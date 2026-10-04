#ifndef APP_UTILITIES_LOG_H
#define APP_UTILITIES_LOG_H

typedef enum LogLevel {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

/* Opens the log file (created 0600). Logging never writes to the terminal,
 * because the TUI owns stdout and stderr. */
void     log_open(const char *path, LogLevel level);
void     log_close(void);
/* Empties the log file and the other *.log files beside it (the backend's),
 * keeping them open. Returns the bytes freed, or -1 when there is no log. */
long long log_clear(void);
LogLevel log_level_parse(const char *name);
/* What this thread's lines are about, put in brackets before each one: the
 * label of the account being worked on. NULL or "" takes it away. The text
 * is copied. log_context_get gives what is set, so it can be put back. */
void     log_context_set(const char *context);
void     log_context_get(char *out, unsigned long size);
void     log_write(LogLevel level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif
