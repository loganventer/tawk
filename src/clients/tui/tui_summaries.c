/* TL;DR in the terminal client: where the conversation gets summaries,
 * folding one message open and shut, and a chat's own switch on its
 * contact card. */
#include "tui_app_state.h"
#include "engines/summary_policy.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static SummaryManager *manager_of(TuiApp *app, AccountId owner) {
    IAccountDirectory *dir = app->deps.directory;
    if (owner != ACCOUNT_ID_NONE && dir) {
        const AccountServices *sv = dir->find(dir, owner);
        if (sv && sv->summaries) return sv->summaries;
    }
    return app->deps.summaries;
}

/* A long message with no summary yet goes on the list of those waiting for one, as it is looked at. */
static int find_summary(void *ctx, const Message *message, AccountId owner, Summary *out) {
    TuiApp *app = ctx;
    SummaryManager *mgr = manager_of(app, owner);
    if (!mgr || message->from_me) return -1;
    if (summary_manager_find(mgr, message->id, out) == 0) {
        if (summary_policy_shorter(message->text, out->text)) return 0;
        summary_dispose(out);                               /* no shorter than the message: the message shows as it is */
        return -1;
    }
    summary_manager_want(mgr, message, messaging_manager_open_chat_info(app->deps.messaging));
    return -1;
}

void tui_app_init_summaries(TuiApp *app) {
    app->summary_source = (SummarySource){ app, find_summary };
}

const SummarySource *tui_app_summaries_for(TuiApp *app, const Chat *chat) {
    SummaryManager *mgr = app->deps.summaries;
    if (!mgr || !chat || chat->soft_locked || !summary_manager_tldr(mgr, chat->jid)) return NULL;
    summary_manager_backfill(mgr, chat, (int64_t)time(NULL));     /* the chat's last weeks, once: see tldr_back_days */
    return &app->summary_source;
}

void tui_app_refresh_summary_prefs(TuiApp *app) {
    ContactPanel *panel = &app->contact;
    SummaryManager *mgr = app->deps.summaries;
    if (!panel->open || !mgr) return;
    contact_panel_set_tldr_pref(panel, summary_manager_tldr(mgr, panel->jid) ? "\xE2\x9C\x93 on" : "off");
    app->dirty = 1;
}

void tui_app_toggle_tldr(TuiApp *app, const char *jid) {
    SummaryManager *mgr = app->deps.summaries;
    if (!mgr) return;
    if (!jid || !jid[0]) { tui_app_toast(app, "Select or open a chat first", 1); return; }
    int on = !summary_manager_tldr(mgr, jid);
    if (summary_manager_set_tldr(mgr, jid, on) != 0) {
        tui_app_toast(app, summary_manager_error(mgr), 1);
        return;
    }
    int n = 0;
    const Message *msgs = tui_app_messages(app, &n);
    message_view_hold(&app->message_view, msgs, n);
    /* Summaries come from a connected agent: with none, say so now, since nothing would seem to happen. */
    const AutomationStatus *st = app->deps.automation ? automation_manager_status(app->deps.automation) : NULL;
    int agents = st ? st->mcp_sessions : 0;
    if (!on) tui_app_toast(app, "TL;DR off for this chat. Its summaries are kept.", 0);
    else if (agents == 0) tui_app_toast(app, "TL;DR on, but no agent is connected to write the summaries (F3 shows agents). Long messages show in full until one is.", 1);
    else {
        char note[200];
        int days = settings_manager_current(app->deps.settings)->tldr_back_days;
        if (days > 0) snprintf(note, sizeof(note), "TL;DR on: long messages here, and those of the last %d days, show as a summary once your agent has written it. Enter unfolds one.", days);
        else snprintf(note, sizeof(note), "TL;DR on: long messages here show as a summary once your agent has written it. Enter unfolds one.");
        tui_app_toast(app, note, 0);
    }
    tui_app_refresh_summary_prefs(app);
    app->dirty = 1;
}

int tui_app_toggle_summary(TuiApp *app, int index) {
    if (!message_view_summarised(&app->message_view, index)) return 0;
    int n = 0;
    const Message *msgs = tui_app_messages(app, &n);
    if (index < 0 || index >= n) return 0;
    message_view_hold(&app->message_view, msgs, n);
    message_view_toggle_summary(&app->message_view, msgs[index].id);
    app->dirty = 1;
    return 1;
}
