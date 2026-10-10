/* The contact card row that says which of a chat's messages alert you:
 * every one, or only one that mentions you. */
#include "tui_app_state.h"

void tui_app_refresh_alerts_pref(TuiApp *app) {
    ContactPanel *panel = &app->contact;
    if (!panel->open) return;
    int mentions = messaging_manager_alert_level(app->deps.messaging, panel->jid) == CHAT_ALERT_MENTIONS;
    contact_panel_set_alerts_pref(panel, mentions ? "only when mentioned" : "for every message");
    app->dirty = 1;
}

void tui_app_step_alerts(TuiApp *app, const char *jid) {
    if (!jid || !jid[0]) return;
    int mentions = messaging_manager_alert_level(app->deps.messaging, jid) == CHAT_ALERT_MENTIONS;
    if (messaging_manager_set_alert_level(app->deps.messaging, jid, mentions ? CHAT_ALERT_ALL : CHAT_ALERT_MENTIONS) != 0) {
        tui_app_toast(app, "That could not be saved", 1);
        return;
    }
    tui_app_refresh_alerts_pref(app);
    tui_app_toast(app, mentions ? "This chat alerts you for every message again, as your settings allow."
                                : "This chat alerts you only when a message mentions you. Its unread count still goes up.", 0);
}
