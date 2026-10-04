#include "utilities/log.h"

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *s_file = NULL;
static char s_dir[1024];                /* folder of the log, for the other logs beside it */
static LogLevel s_level = LOG_LEVEL_INFO;
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_name(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
    }
    return "?";
}

void log_open(const char *path, LogLevel level) {
    pthread_mutex_lock(&s_mutex);
    s_level = level;
    if (s_file) { fclose(s_file); s_file = NULL; }
    s_dir[0] = '\0';
    if (path && path[0]) {
        const char *slash = strrchr(path, '/');
        if (slash && (size_t)(slash - path) < sizeof(s_dir)) { memcpy(s_dir, path, (size_t)(slash - path)); s_dir[slash - path] = '\0'; }
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            s_file = fdopen(fd, "a");
            if (!s_file) close(fd);
        }
    }
    pthread_mutex_unlock(&s_mutex);
}

void log_close(void) {
    pthread_mutex_lock(&s_mutex);
    if (s_file) { fclose(s_file); s_file = NULL; }
    pthread_mutex_unlock(&s_mutex);
}

LogLevel log_level_parse(const char *name) {
    if (!name) return LOG_LEVEL_INFO;
    if (strcasecmp(name, "debug") == 0) return LOG_LEVEL_DEBUG;
    if (strcasecmp(name, "warn") == 0) return LOG_LEVEL_WARN;
    if (strcasecmp(name, "error") == 0) return LOG_LEVEL_ERROR;
    return LOG_LEVEL_INFO;
}

static _Thread_local char s_context[64];

void log_context_set(const char *context) {
    snprintf(s_context, sizeof(s_context), "%s", context ? context : "");
}

void log_context_get(char *out, unsigned long size) {
    snprintf(out, size, "%s", s_context);
}

void log_write(LogLevel level, const char *fmt, ...) {
    pthread_mutex_lock(&s_mutex);
    if (s_file && level >= s_level) {
        char stamp[32];
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_now);
        fprintf(s_file, "%s [%s] ", stamp, level_name(level));
        if (s_context[0]) fprintf(s_file, "[%s] ", s_context);
        va_list args;
        va_start(args, fmt);
        vfprintf(s_file, fmt, args);
        va_end(args);
        fputc('\n', s_file);
        fflush(s_file);
    }
    pthread_mutex_unlock(&s_mutex);
}

long long log_clear(void) {
    pthread_mutex_lock(&s_mutex);
    if (!s_dir[0]) { pthread_mutex_unlock(&s_mutex); return -1; }
    long long freed = 0;
    DIR *d = opendir(s_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t len = strlen(e->d_name);
            if (len < 5 || strcmp(e->d_name + len - 4, ".log") != 0) continue;
            char path[1300];
            snprintf(path, sizeof(path), "%s/%s", s_dir, e->d_name);
            /* truncate in place: writers that append (the backend) carry on safely */
            int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0) continue;
            struct stat st;
            if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && ftruncate(fd, 0) == 0) freed += st.st_size;
            close(fd);
        }
        closedir(d);
    }
    if (s_file) fflush(s_file);
    pthread_mutex_unlock(&s_mutex);
    return freed;
}
