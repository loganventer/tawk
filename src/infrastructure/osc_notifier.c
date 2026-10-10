#include "infrastructure/osc_notifier.h"
#include "engines/banner_text.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct OscNotifier {
    const Settings *settings;
    int             fd;
    int             osc;
} OscNotifier;

static void write_all(int fd, const char *s, size_t len) {
    while (fd >= 0 && len > 0) {
        ssize_t n = write(fd, s, len);
        if (n <= 0) return;
        s += n;
        len -= (size_t)n;
    }
}

static void osc_notify(INotifier *self, const Notification *n) {
    OscNotifier *o = self->ctx;
    const char *mode = o->settings->system_notifications;
    if (strcmp(mode, "terminal") != 0 && strcmp(mode, "both") != 0) return;
    char title[200], body[300], seq[700];
    banner_text_title(n, title, sizeof(title));
    banner_text_body(n, body, sizeof(body));
    int len;
    if (o->osc == 99) {
        len = snprintf(seq, sizeof(seq), "\033]99;i=tawk:d=0;%s\033\\\033]99;i=tawk:d=1:p=body;%s\033\\", title, body);
    } else if (o->osc == 9) {
        len = snprintf(seq, sizeof(seq), "\033]9;%s: %s\007", title, body);
    } else {
        len = snprintf(seq, sizeof(seq), "\033]777;notify;%s;%s\007", title, body);
    }
    if (len > 0) write_all(o->fd, seq, (size_t)len < sizeof(seq) ? (size_t)len : sizeof(seq) - 1);
}

static void osc_destroy(INotifier *self) {
    OscNotifier *o = self->ctx;
    if (o->fd >= 0) close(o->fd);
    free(o);
    free(self);
}

INotifier *osc_notifier_create(const Settings *settings) {
    INotifier *n = calloc(1, sizeof(*n));
    OscNotifier *o = calloc(1, sizeof(*o));
    if (!n || !o) { free(n); free(o); return NULL; }
    o->settings = settings;
    o->fd = open("/dev/tty", O_WRONLY | O_NOCTTY | O_CLOEXEC);
    o->osc = banner_text_osc(getenv("TERM_PROGRAM"), getenv("KITTY_WINDOW_ID"), getenv("TERM"));
    n->ctx = o;
    n->notify = osc_notify;
    n->destroy = osc_destroy;
    return n;
}
