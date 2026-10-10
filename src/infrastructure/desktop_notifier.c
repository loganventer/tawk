#include "infrastructure/desktop_notifier.h"
#include "engines/banner_text.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct DesktopNotifier {
    const Settings *settings;
    char            program[PATH_MAX];      /* the full path, "" when there is none */
    int             kind;                   /* which of the three it is */
} DesktopNotifier;

enum { KIND_NONE = 0, KIND_TERMINAL_NOTIFIER, KIND_OSASCRIPT, KIND_NOTIFY_SEND };

static int on_path(const char *name, char *out, size_t size) {
    const char *path = getenv("PATH");
    if (!path) return 0;
    while (*path) {
        const char *end = strchr(path, ':');
        size_t len = end ? (size_t)(end - path) : strlen(path);
        if (len > 0 && path[0] == '/' && snprintf(out, size, "%.*s/%s", (int)len, path, name) < (int)size && access(out, X_OK) == 0) return 1;
        if (!end) break;
        path = end + 1;
    }
    out[0] = '\0';
    return 0;
}

static int find_program(char *out, size_t size) {
#ifdef __APPLE__
    if (on_path("terminal-notifier", out, size)) return KIND_TERMINAL_NOTIFIER;
    if (on_path("osascript", out, size)) return KIND_OSASCRIPT;
#endif
    if (on_path("notify-send", out, size)) return KIND_NOTIFY_SEND;
    return KIND_NONE;
}

const char *desktop_notifier_program(void) {
    static char found[PATH_MAX];
    return find_program(found, sizeof(found)) != KIND_NONE ? found : "";
}

/* Runs the program and does not wait for it: a child that forks again and exits, so nothing is left behind. */
static void run(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        if (fork() == 0) {
            FILE *quiet = freopen("/dev/null", "r", stdin);
            quiet = freopen("/dev/null", "w", stdout);
            quiet = freopen("/dev/null", "w", stderr);
            (void)quiet;
            execv(argv[0], argv);
        }
        _exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
}

static void desktop_notify(INotifier *self, const Notification *n) {
    DesktopNotifier *d = self->ctx;
    const char *mode = d->settings->system_notifications;
    if (d->kind == KIND_NONE || (strcmp(mode, "desktop") != 0 && strcmp(mode, "both") != 0)) return;
    char title[200], body[300];
    banner_text_title(n, title, sizeof(title));
    banner_text_body(n, body, sizeof(body));
    if (d->kind == KIND_TERMINAL_NOTIFIER) {
        char *argv[] = { d->program, "-title", title, "-message", body, "-group", "tawk", NULL };
        run(argv);
    } else if (d->kind == KIND_OSASCRIPT) {
        /* The two lines travel as arguments of the script, never inside its text. */
        char *argv[] = { d->program, "-e", "on run argv", "-e", "display notification (item 2 of argv) with title (item 1 of argv)",
                         "-e", "end run", title, body, NULL };
        run(argv);
    } else {
        char *argv[] = { d->program, "-a", "tawk", "--", title, body, NULL };
        run(argv);
    }
}

static void desktop_destroy(INotifier *self) {
    free(self->ctx);
    free(self);
}

INotifier *desktop_notifier_create(const Settings *settings) {
    INotifier *n = calloc(1, sizeof(*n));
    DesktopNotifier *d = calloc(1, sizeof(*d));
    if (!n || !d) { free(n); free(d); return NULL; }
    d->settings = settings;
    d->kind = find_program(d->program, sizeof(d->program));
    n->ctx = d;
    n->notify = desktop_notify;
    n->destroy = desktop_destroy;
    return n;
}
