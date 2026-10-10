/* The contact card row that says what agents may do in one chat, on top of
 * what the account allows: as the account says, always ask, read only, or
 * hidden from them. It steps to the next choice. */
#include "tui_app_state.h"
#include "engines/chat_agent_rules.h"

void tui_app_refresh_agent_rule_pref(TuiApp *app) {
    ContactPanel *panel = &app->contact;
    if (!panel->open || !app->deps.automation) return;
    contact_panel_set_agent_rule_pref(panel, chat_agent_rules_label(automation_manager_chat_rule(app->deps.automation, panel->jid)));
    app->dirty = 1;
}

void tui_app_step_agent_rule(TuiApp *app, const char *jid) {
    AutomationManager *mgr = app->deps.automation;
    if (!mgr || !jid || !jid[0]) return;
    ChatAgentRule next = chat_agent_rules_next(automation_manager_chat_rule(mgr, jid));
    if (automation_manager_set_chat_rule(mgr, jid, next) != 0) { tui_app_toast(app, "The rule could not be saved", 1); return; }
    tui_app_refresh_agent_rule_pref(app);
    switch (next) {
        case CHAT_AGENT_ALWAYS_ASK:
            tui_app_toast(app, "Agents here: every send in this chat is asked about, each time. No \"for this session\", and none answers its own.", 0);
            break;
        case CHAT_AGENT_NO_SEND:
            tui_app_toast(app, "Agents here: they may read this chat and never write in it.", 0);
            break;
        case CHAT_AGENT_HIDDEN:
            tui_app_toast(app, "Agents here: this chat is hidden from them. They cannot list, read or find it, on any of your numbers.", 0);
            break;
        default:
            tui_app_toast(app, "Agents here: as the account says.", 0);
            break;
    }
}
