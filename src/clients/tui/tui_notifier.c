#include "clients/tui/tui_notifier.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <ncurses.h>
#include <stdlib.h>

typedef struct TuiNotifier {
    BlinkState     *blink;
    TitleFlasher   *flasher;
    ITerminalTitle *title;
    const Settings *settings;
} TuiNotifier;

static void tn_notify(INotifier *self, const Notification *n) {
    TuiNotifier *t = self->ctx;
    int64_t now = clock_now_ms();
    if (t->settings->blink) {
        str_copy(t->blink->jid, sizeof(t->blink->jid), n->chat_jid);
        t->blink->account = n->account;
        t->blink->until_ms = now + (int64_t)t->settings->blink_seconds * 1000;
    }
    if (t->settings->title_flash) title_flasher_start(t->flasher, now);
    if (t->settings->terminal_bell && t->title) t->title->alert(t->title);
    if (t->settings->screen_flash) flash();
}

static void tn_destroy(INotifier *self) {
    free(self->ctx);
    free(self);
}

INotifier *tui_notifier_create(BlinkState *blink, TitleFlasher *flasher, ITerminalTitle *title,
                               const Settings *settings) {
    INotifier *n = calloc(1, sizeof(*n));
    TuiNotifier *t = calloc(1, sizeof(*t));
    if (!n || !t) { free(n); free(t); return NULL; }
    *t = (TuiNotifier){ blink, flasher, title, settings };
    n->ctx = t;
    n->notify = tn_notify;
    n->destroy = tn_destroy;
    return n;
}
