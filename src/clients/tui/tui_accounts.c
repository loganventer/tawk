/* The accounts that are running besides the one in view. Each has its own
 * managers, and every one is served every frame: an account nobody looks at
 * still receives, notifies and sends what is due. */
#include "tui_app_state.h"

/* Every running account other than the one whose managers are in app->deps. */
static const AccountServices *other(TuiApp *app, int index) {
    IAccountDirectory *dir = app->deps.directory;
    if (!dir) return NULL;
    const AccountServices *sv = dir->at(dir, index);
    return sv;
}

static int in_view(const TuiApp *app, const AccountServices *sv) {
    return sv->messaging == app->deps.messaging;
}

void tui_app_accounts_start(TuiApp *app) {
    messaging_manager_start(app->deps.messaging);
    for (int i = 0; ; i++) {
        const AccountServices *sv = other(app, i);
        if (!sv) break;
        if (!in_view(app, sv)) messaging_manager_start(sv->messaging);
    }
}

void tui_app_accounts_tick(TuiApp *app) {
    for (int i = 0; ; i++) {
        const AccountServices *sv = other(app, i);
        if (!sv) break;
        if (in_view(app, sv)) continue;
        ManagerChanges ch;
        messaging_manager_tick(sv->messaging, &ch);
        profile_manager_tick(sv->profiles);
        if (scheduling_manager_take_changed(sv->scheduling)) app->dirty = 1;
        int late = 0;
        int sent = tui_scheduling_send_due(sv->scheduling, sv->messaging, &late);
        tui_app_scheduling_report(app, sent, late);
        if (ch.chats || ch.messages || ch.auth || ch.notified) app->dirty = 1;
    }
}

void tui_app_accounts_set_active(TuiApp *app, int active) {
    for (int i = 0; ; i++) {
        const AccountServices *sv = other(app, i);
        if (!sv) break;
        if (!in_view(app, sv)) messaging_manager_set_active(sv->messaging, active);
    }
}
