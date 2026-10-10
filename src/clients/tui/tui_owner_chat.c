/* The contact card row that names your own "message yourself" chat as your
 * chat with the agent. It shows on that chat alone, and is kept in the
 * settings under Automation, where an agent cannot change it. */
#include "tui_app_state.h"
#include "engines/self_chat_rule.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>

static int is_own_chat(TuiApp *app, const char *jid) {
    return self_chat_rule_is(messaging_manager_user_jid(app->deps.messaging), jid);
}

static int named(TuiApp *app) {
    const Settings *s = settings_manager_current(app->deps.settings);
    return s->owner_chat[0] && atoi(s->owner_chat) == app->deps.active_account;
}

void tui_app_refresh_owner_pref(TuiApp *app) {
    ContactPanel *panel = &app->contact;
    if (!panel->open) return;
    if (!is_own_chat(app, panel->jid)) { contact_panel_set_owner_pref(panel, ""); return; }
    contact_panel_set_owner_pref(panel, named(app) ? "\xE2\x9C\x93 on" : "off");
    app->dirty = 1;
}

void tui_app_toggle_owner_chat(TuiApp *app, const char *jid) {
    if (!jid || !is_own_chat(app, jid)) return;
    Settings s = *settings_manager_current(app->deps.settings);
    int on = !named(app);
    if (on) snprintf(s.owner_chat, sizeof(s.owner_chat), "%d", app->deps.active_account);
    else s.owner_chat[0] = '\0';
    if (settings_manager_apply(app->deps.settings, &s) != 0) { tui_app_toast(app, "The setting could not be saved", 1); return; }
    tui_app_refresh_owner_pref(app);
    if (on) tui_app_toast(app, "This is your chat with the agent now. What you type here on your phone reaches your agent as your words, it answers "
                               "here by itself, and sends that wait for you are put to you here. Anyone at a device linked to this number can do the same.", 0);
    else tui_app_toast(app, "This is an ordinary chat again: an agent takes nothing here as your words and asks before it sends.", 0);
}
