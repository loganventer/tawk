/* The accounts that are running besides the one in view. Each has its own
 * managers, and every one is served every frame: an account nobody looks at
 * still receives, notifies and sends what is due. */
#include "tui_app_state.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

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

/* ---- the chat list across accounts ---------------------------------------- */

#define ROWS_REFRESH_MS 1000     /* typing marks and the like change without a chat changing */

int tui_app_account_count(TuiApp *app) {
    IAccountDirectory *dir = app->deps.directory;
    return dir ? dir->count(dir) : 1;
}

static void contact_prefs(void *ctx, const char *jid, ChatPrefs *out) {
    TuiApp *app = ctx;
    if (app->deps.roster) account_roster_manager_chat_prefs(app->deps.roster, jid, out);
}

/* The badge of each running account, in the order the rows' bits name them. */
static void make_badges(TuiApp *app) {
    IAccountDirectory *dir = app->deps.directory;
    app->chat_list.badge_count = 0;
    for (int i = 0; dir && i < ACCOUNT_MAX; i++) {
        const AccountServices *sv = dir->at(dir, i);
        if (!sv) break;
        Account account;
        memset(&account, 0, sizeof(account));
        account.id = sv->id;
        if (!app->deps.roster || account_roster_manager_get(app->deps.roster, sv->id, &account) != 0) {
            str_copy(account.label, sizeof(account.label), sv->label ? sv->label : "");
        }
        account_badge_make(&app->chat_list.badges[app->chat_list.badge_count++], &account);
    }
}

const Chat *tui_app_chat_rows(TuiApp *app, int *count) {
    IAccountDirectory *dir = app->deps.directory;
    int accounts = tui_app_account_count(app);
    if (!dir || accounts <= 1) {                              /* one account: its own list, as it always was */
        app->chat_list.badge_count = 0;
        return messaging_manager_chats(app->deps.messaging, count);
    }
    int64_t now = clock_now_ms();
    if (app->chat_rows_stale || now - app->chat_rows_built_ms > ROWS_REFRESH_MS) {
        ChatSource sources[ACCOUNT_MAX];
        int n = 0;
        for (int i = 0; i < accounts && n < ACCOUNT_MAX; i++) {
            const AccountServices *sv = dir->at(dir, i);
            if (!sv) break;
            sources[n].account = sv->id;
            sources[n].chats = messaging_manager_chats(sv->messaging, &sources[n].count);
            n++;
        }
        UnifiedChatRules rules = {
            .only = app->account_filter,
            .merge_setting = settings_manager_current(app->deps.settings)->merge_accounts,
            .primary = app->deps.roster ? account_roster_manager_primary(app->deps.roster) : ACCOUNT_ID_NONE,
            .prefs = contact_prefs,
            .ctx = app,
        };
        unified_chat_list_build(&app->chat_rows, sources, n, &rules);
        make_badges(app);
        app->chat_rows_stale = 0;
        app->chat_rows_built_ms = now;
    }
    *count = app->chat_rows.count;
    return app->chat_rows.rows;
}

const char *tui_app_take_selected(TuiApp *app) {
    int count = 0;
    const Chat *rows = tui_app_chat_rows(app, &count);
    const Chat *row = chat_list_view_selected_chat(&app->chat_list, rows);
    if (!row) return NULL;
    static char jid[128];                                     /* outlives the rows, which the switch may rebuild */
    str_copy(jid, sizeof(jid), row->jid);
    if (tui_app_use_account(app, row->account) != 0) return NULL;
    return jid;
}

void tui_app_set_account_filter(TuiApp *app, AccountId account) {
    if (account != ACCOUNT_ID_NONE && tui_app_use_account(app, account) != 0) return;
    app->account_filter = account;
    app->chat_rows_stale = 1;
    app->dirty = 1;
}

/* All, then each account in turn, then All again. */
void tui_app_cycle_account_filter(TuiApp *app) {
    IAccountDirectory *dir = app->deps.directory;
    int n = tui_app_account_count(app);
    if (!dir || n <= 1) return;
    int next = 0;                                             /* the first account, after All */
    for (int i = 0; app->account_filter != ACCOUNT_ID_NONE && i < n; i++) {
        const AccountServices *sv = dir->at(dir, i);
        if (sv && sv->id == app->account_filter) { next = i + 1; break; }
    }
    const AccountServices *sv = next < n ? dir->at(dir, next) : NULL;
    tui_app_set_account_filter(app, sv ? sv->id : ACCOUNT_ID_NONE);
    char msg[ACCOUNT_LABEL_SIZE + 48];
    if (sv) snprintf(msg, sizeof(msg), "Showing the chats of %s", sv->label);
    else snprintf(msg, sizeof(msg), "Showing the chats of every account");
    tui_app_toast(app, msg, 0);
}

void tui_app_account_chip(TuiApp *app, char *out, size_t size) {
    if (size) out[0] = '\0';
    IAccountDirectory *dir = app->deps.directory;
    if (!dir || tui_app_account_count(app) <= 1) return;
    if (app->account_filter == ACCOUNT_ID_NONE) { str_copy(out, size, "All"); return; }
    const AccountServices *sv = dir->find(dir, app->account_filter);
    str_copy(out, size, sv && sv->label ? sv->label : "All");
}
