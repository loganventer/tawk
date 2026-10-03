/* The accounts that are running besides the one in view. Each has its own
 * managers, and every one is served every frame: an account nobody looks at
 * still receives, notifies and sends what is due. */
#include "tui_app_state.h"
#include "engines/jid_list.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* ---- the accounts dialog -------------------------------------------------- */

void tui_app_open_accounts(TuiApp *app) {
    settings_panel_close(&app->settings_panel);
    accounts_dialog_open(&app->accounts_dialog);
    app->accounts_dialog.selected = -1;                      /* the render picks the account in view */
    app->dirty = 1;
}

static int unread_chats(MessagingManager *messaging) {
    int count = 0, unread = 0;
    const Chat *chats = messaging_manager_chats(messaging, &count);
    for (int i = 0; i < count; i++) unread += chats[i].unread > 0 && !chats[i].is_archived && chats[i].is_locked != 1;
    return unread;
}

/* The roster's accounts with how each stands now. */
static int dialog_rows(TuiApp *app, AccountsDialogRow *rows) {
    Account all[ACCOUNT_MAX];
    int n = app->deps.roster ? account_roster_manager_list(app->deps.roster, all, ACCOUNT_MAX) : 0;
    IAccountDirectory *dir = app->deps.directory;
    for (int i = 0; i < n; i++) {
        const AccountServices *sv = dir ? dir->find(dir, all[i].id) : NULL;
        rows[i].account = all[i];
        rows[i].auth = sv ? messaging_manager_auth_state(sv->messaging) : AUTH_STATE_FAILED;
        rows[i].in_view = all[i].id == app->deps.active_account;
        rows[i].unread = sv ? unread_chats(sv->messaging) : 0;
    }
    return n;
}

void tui_app_accounts_render(TuiApp *app, UiRect area) {
    AccountsDialogRow rows[ACCOUNT_MAX];
    int n = dialog_rows(app, rows);
    AccountsDialog *d = &app->accounts_dialog;
    if (d->selected < 0) {
        d->count = n;
        for (int i = 0; i < n; i++) d->ids[i] = rows[i].account.id;
        d->selected = 0;
        accounts_dialog_select(d, app->deps.active_account);
    }
    accounts_dialog_render(d, area, rows, n);
}

/* What agents may do, one step on: a new account goes from nothing to reading. */
static AccountAgentAccess next_access(AccountAgentAccess now) {
    switch (now) {
        case ACCOUNT_AGENT_OFF:    return ACCOUNT_AGENT_READ;
        case ACCOUNT_AGENT_FOLLOW: return ACCOUNT_AGENT_OFF;
        case ACCOUNT_AGENT_READ:   return ACCOUNT_AGENT_SEND;
        case ACCOUNT_AGENT_SEND:   return ACCOUNT_AGENT_MANAGE;
        case ACCOUNT_AGENT_MANAGE: return ACCOUNT_AGENT_ADMIN;
        default:                   return ACCOUNT_AGENT_OFF;
    }
}

static void add_account(TuiApp *app) {
    AccountsDialog *d = &app->accounts_dialog;
    AccountRosterManager *roster = app->deps.roster;
    IAccountDirectory *dir = app->deps.directory;
    char *label = accounts_dialog_label(d);
    AccountId id = ACCOUNT_ID_NONE;
    int added = label && account_roster_manager_add(roster, label, (int64_t)time(NULL), &id) == 0;
    free(label);
    if (!added) { accounts_dialog_error(d, account_roster_manager_error(roster)); return; }
    const AccountServices *sv = dir->start(dir, id);
    if (!sv) {
        account_roster_manager_remove(roster, id);
        accounts_dialog_error(d, "The account could not be started; see the log.");
        return;
    }
    messaging_manager_start(sv->messaging);
    accounts_dialog_done(d);
    d->open = 0;
    /* In view, its linking wizard shows; the other accounts carry on behind it. */
    tui_app_use_account(app, id);
    char msg[ACCOUNT_LABEL_SIZE + 64];
    snprintf(msg, sizeof(msg), "Link %s with the phone that has that number", sv->label);
    tui_app_toast(app, msg, 0);
}

void tui_app_accounts_request(TuiApp *app, AccountsDialogRequest request) {
    AccountsDialog *d = &app->accounts_dialog;
    AccountRosterManager *roster = app->deps.roster;
    IAccountDirectory *dir = app->deps.directory;
    AccountId id = accounts_dialog_selected(d);
    Account account;
    int have = roster && id != ACCOUNT_ID_NONE && account_roster_manager_get(roster, id, &account) == 0;
    app->dirty = 1;
    if (!roster || !dir) return;
    switch (request) {
        case ACCOUNTS_REQUEST_VIEW:
            if (have && tui_app_use_account(app, id) == 0) {
                d->open = 0;
                app->account_filter = ACCOUNT_ID_NONE;
                char msg[ACCOUNT_LABEL_SIZE + 32];
                snprintf(msg, sizeof(msg), "%s is in view", account.label);
                tui_app_toast(app, msg, 0);
            }
            break;
        case ACCOUNTS_REQUEST_ADD:
            add_account(app);
            break;
        case ACCOUNTS_REQUEST_RENAME: {
            char *label = accounts_dialog_label(d);
            if (label && account_roster_manager_rename(roster, id, label) == 0) {
                dir->relabel(dir, id);
                accounts_dialog_done(d);
            } else {
                accounts_dialog_error(d, account_roster_manager_error(roster));
            }
            free(label);
            break;
        }
        case ACCOUNTS_REQUEST_PRIMARY:
            if (have && account_roster_manager_set_primary(roster, id) != 0) accounts_dialog_error(d, account_roster_manager_error(roster));
            break;
        case ACCOUNTS_REQUEST_ACCESS:
            if (have && account_roster_manager_set_agent_access(roster, id, next_access(account.agent_access)) != 0) {
                accounts_dialog_error(d, account_roster_manager_error(roster));
            }
            break;
        case ACCOUNTS_REQUEST_LOGOUT: {
            if (!have) break;
            char subject[16], question[ACCOUNT_LABEL_SIZE + 64];
            snprintf(subject, sizeof(subject), "%d", id);
            snprintf(question, sizeof(question), "Unlink %s from WhatsApp?", account.label);
            confirm_dialog_open(&app->confirm, CONFIRM_LOGOUT_ACCOUNT, subject, "Log out", question,
                                "Its chats stay on this computer. To use it again you link it with the phone once more.", "Log out", 0);
            break;
        }
        case ACCOUNTS_REQUEST_REMOVE: {
            if (!have) break;
            const AccountServices *sv = dir->find(dir, id);
            if (dir->count(dir) <= 1) {
                accounts_dialog_error(d, "The last account cannot be removed. Log it out instead.");
            } else if (sv && messaging_manager_auth_state(sv->messaging) != AUTH_STATE_NEEDS_LOGIN) {
                accounts_dialog_error(d, "Log this account out first (l), so it is unlinked on the phone too.");
            } else {
                char subject[16], question[ACCOUNT_LABEL_SIZE + 64];
                snprintf(subject, sizeof(subject), "%d", id);
                snprintf(question, sizeof(question), "Remove %s from tawk?", account.label);
                confirm_dialog_open(&app->confirm, CONFIRM_REMOVE_ACCOUNT, subject, "Remove account", question,
                                    "Every chat, message and contact kept for it on this computer is deleted. This cannot be undone.",
                                    "Remove", 1);
            }
            break;
        }
        case ACCOUNTS_REQUEST_SENDING:
            tui_app_open_send_accounts(app);
            break;
        default:
            break;
    }
    app->chat_rows_stale = 1;
}

void tui_app_logout_account(TuiApp *app, AccountId account) {
    IAccountDirectory *dir = app->deps.directory;
    const AccountServices *sv = dir ? dir->find(dir, account) : NULL;
    if (!sv) return;
    messaging_manager_logout(sv->messaging);
    char msg[ACCOUNT_LABEL_SIZE + 64];
    snprintf(msg, sizeof(msg), "%s is logged out; link it again with a QR code or phone number", sv->label);
    tui_app_toast(app, msg, 0);
    app->dirty = 1;
}

void tui_app_remove_account(TuiApp *app, AccountId account) {
    IAccountDirectory *dir = app->deps.directory;
    AccountRosterManager *roster = app->deps.roster;
    if (!dir || !roster || dir->count(dir) <= 1) return;
    if (account == app->deps.active_account) {                /* another account takes its place in view */
        AccountId next = ACCOUNT_ID_NONE;
        for (int i = 0; next == ACCOUNT_ID_NONE; i++) {
            const AccountServices *sv = dir->at(dir, i);
            if (!sv) break;
            if (sv->id != account) next = sv->id;
        }
        if (next == ACCOUNT_ID_NONE || tui_app_use_account(app, next) != 0) return;
    }
    if (app->account_filter == account) app->account_filter = ACCOUNT_ID_NONE;
    dir->forget(dir, account);
    if (account_roster_manager_remove(roster, account) != 0) tui_app_toast(app, account_roster_manager_error(roster), 1);
    else tui_app_toast(app, "\xF0\x9F\x97\x91 Account removed", 0);
    app->accounts_dialog.selected = -1;
    app->chat_rows_stale = 1;
    app->dirty = 1;
}

void tui_app_open_send_accounts(TuiApp *app) {
    app->accounts_dialog.open = 0;
    send_account_dialog_open(&app->send_accounts);
    app->dirty = 1;
}

void tui_app_send_accounts_render(TuiApp *app, UiRect area) {
    AccountRosterManager *roster = app->deps.roster;
    ChatPrefs own[SEND_ACCOUNT_ROWS];
    SendAccountRow rows[SEND_ACCOUNT_ROWS];
    int n = roster ? account_roster_manager_send_accounts(roster, own, SEND_ACCOUNT_ROWS) : 0;
    int count = 0;
    const Chat *chats = tui_app_chat_rows(app, &count);
    for (int i = 0; i < n; i++) {
        Account account;
        memset(&rows[i], 0, sizeof(rows[i]));
        str_copy(rows[i].jid, sizeof(rows[i].jid), own[i].jid);
        for (int k = 0; k < count; k++) {
            if (strcmp(chats[k].jid, own[i].jid) == 0) { str_copy(rows[i].name, sizeof(rows[i].name), chats[k].name); break; }
        }
        if (account_roster_manager_get(roster, own[i].send_account, &account) == 0) {
            str_copy(rows[i].account, sizeof(rows[i].account), account.label);
        }
    }
    Account primary;
    memset(&primary, 0, sizeof(primary));
    if (roster) account_roster_manager_get(roster, account_roster_manager_primary(roster), &primary);
    send_account_dialog_render(&app->send_accounts, area, rows, n, primary.label);
}

void tui_app_send_accounts_request(TuiApp *app, SendAccountRequest request) {
    AccountRosterManager *roster = app->deps.roster;
    const char *selected = send_account_dialog_selected(&app->send_accounts);
    app->dirty = 1;
    if (!roster || !selected) return;
    char jid[128];
    str_copy(jid, sizeof(jid), selected);
    if (request == SEND_ACCOUNT_REQUEST_STEP) {
        tui_app_step_send_from(app, jid);
    } else if (request == SEND_ACCOUNT_REQUEST_RESET) {
        account_roster_manager_set_send_account(roster, jid, ACCOUNT_ID_NONE);
        app->chat_rows_stale = 1;
    }
}

/* ---- a conversation merged across accounts -------------------------------- */

void tui_app_take_services(TuiApp *app, const AccountServices *sv) {
    app->deps.messaging = sv->messaging;
    app->deps.profiles = sv->profiles;
    app->deps.calls = sv->calls;
    app->deps.accounts = sv->accounts;
    app->deps.statuses = sv->statuses;
    app->deps.feed = sv->feed;
    app->deps.scheduling = sv->scheduling;
    app->deps.backend_name = sv->backend_name;
    app->deps.active_account = sv->id;
}

static const AccountServices *peer(TuiApp *app, int index) {
    IAccountDirectory *dir = app->deps.directory;
    return dir && index >= 0 && index < app->peer_count ? dir->find(dir, app->peers[index]) : NULL;
}

void tui_app_close_peers(TuiApp *app) {
    for (int i = 0; i < app->peer_count; i++) {
        const AccountServices *sv = peer(app, i);
        if (sv) messaging_manager_open_chat(sv->messaging, "");   /* out of view: its unread counts run again */
    }
    app->peer_count = 0;
}

void tui_app_open_peers(TuiApp *app, const Chat *row) {
    IAccountDirectory *dir = app->deps.directory;
    tui_app_close_peers(app);
    if (!dir || !(row->accounts & (row->accounts - 1))) return;     /* on one account only */
    for (int i = 0; i < ACCOUNT_MAX; i++) {
        if (!(row->accounts & (1u << i))) continue;
        const AccountServices *sv = dir->at(dir, i);
        if (!sv || sv->id == app->deps.active_account) continue;
        messaging_manager_open_chat(sv->messaging, row->jid);
        app->peers[app->peer_count++] = sv->id;
    }
    app->chat_rows_stale = 1;
    app->dirty = 1;
}

const Message *tui_app_messages(TuiApp *app, int *count) {
    if (app->peer_count == 0) return messaging_manager_messages(app->deps.messaging, count);
    MessageSource sources[ACCOUNT_MAX];
    int n = 0;
    sources[n].account = app->deps.active_account;
    sources[n].messages = messaging_manager_messages(app->deps.messaging, &sources[n].count);
    n++;
    for (int i = 0; i < app->peer_count && n < ACCOUNT_MAX; i++) {
        const AccountServices *sv = peer(app, i);
        if (!sv) continue;
        sources[n].account = sv->id;
        sources[n].messages = messaging_manager_messages(sv->messaging, &sources[n].count);
        n++;
    }
    merged_message_window_build(&app->merged, sources, n, app->deps.active_account);
    *count = app->merged.count;
    return app->merged.items;
}

AccountId tui_app_message_account(TuiApp *app, int index) {
    if (app->peer_count == 0) return app->deps.active_account;
    int count = 0;
    tui_app_messages(app, &count);
    return index >= 0 && index < count ? app->merged.owners[index] : app->deps.active_account;
}

int tui_app_turn_to(TuiApp *app, AccountId account) {
    if (account == ACCOUNT_ID_NONE || account == app->deps.active_account) return 0;
    for (int i = 0; i < app->peer_count; i++) {
        if (app->peers[i] != account) continue;
        const AccountServices *sv = peer(app, i);
        if (!sv) return -1;
        app->peers[i] = app->deps.active_account;           /* the two change places; the chat stays open in both */
        tui_app_take_services(app, sv);
        app->chat_list.open_account = account;
        app->conversation_theme_valid = 0;
        app->chat_rows_stale = 1;
        app->dirty = 1;
        return 0;
    }
    return tui_app_use_account(app, account);
}

static void say_sending_as(TuiApp *app) {
    IAccountDirectory *dir = app->deps.directory;
    const AccountServices *sv = dir ? dir->find(dir, app->deps.active_account) : NULL;
    char msg[ACCOUNT_LABEL_SIZE + 32];
    snprintf(msg, sizeof(msg), "Sending as %s", sv && sv->label ? sv->label : "this account");
    tui_app_toast(app, msg, 0);
}

void tui_app_follow_message(TuiApp *app, int index) {
    if (app->peer_count == 0) return;
    AccountId owner = tui_app_message_account(app, index);
    if (owner == app->deps.active_account) return;
    if (tui_app_turn_to(app, owner) == 0) say_sending_as(app);
}

void tui_app_cycle_send_account(TuiApp *app) {
    if (app->peer_count == 0) {
        tui_app_toast(app, tui_app_account_count(app) > 1 ? "This chat is on one of your accounts only" : "There is one account", 0);
        return;
    }
    /* The next id up among the accounts that share the chat, then round again. */
    AccountId now = app->deps.active_account, next = ACCOUNT_ID_NONE, lowest = now;
    for (int i = 0; i < app->peer_count; i++) {
        AccountId id = app->peers[i];
        if (id < lowest) lowest = id;
        if (id > now && (next == ACCOUNT_ID_NONE || id < next)) next = id;
    }
    if (tui_app_turn_to(app, next != ACCOUNT_ID_NONE ? next : lowest) == 0) say_sending_as(app);
}

void tui_app_send_label(TuiApp *app, char *out, size_t size) {
    if (size) out[0] = '\0';
    IAccountDirectory *dir = app->deps.directory;
    if (!dir || tui_app_account_count(app) <= 1 || !messaging_manager_open_jid(app->deps.messaging)[0]) return;
    const AccountServices *sv = dir->find(dir, app->deps.active_account);
    if (sv && sv->label) snprintf(out, size, "as %s%s", sv->label, app->peer_count ? " (Alt+A changes)" : "");
}

int tui_app_peers_load_older(TuiApp *app) {
    int older = 0;
    for (int i = 0; i < app->peer_count; i++) {
        const AccountServices *sv = peer(app, i);
        if (sv && messaging_manager_load_older(sv->messaging)) older = 1;
    }
    return older;
}

/* ---- a contact's own settings, on the contact card ------------------------- */

static const Settings *current_settings(TuiApp *app) { return settings_manager_current(app->deps.settings); }

/* What agents may do with an account, following the setting where it says to. */
static AccountAgentAccess effective_access(TuiApp *app, const Account *account) {
    if (account->agent_access != ACCOUNT_AGENT_FOLLOW) return account->agent_access;
    return account_agent_access_parse(current_settings(app)->automation_access);
}

/* The chats an agent may answer by itself in for an account. An account
 * that follows the setting and has no list of its own still uses the one
 * the settings held before accounts had their own. */
static void self_approval_chats(TuiApp *app, const Account *account, char *out, size_t size) {
    out[0] = '\0';
    account_roster_manager_self_approval_chats(app->deps.roster, account->id, out, size);
    if (!out[0] && account->agent_access == ACCOUNT_AGENT_FOLLOW) str_copy(out, size, current_settings(app)->automation_self_chats);
}

static const char *label_of(TuiApp *app, AccountId id, Account *scratch) {
    if (id != ACCOUNT_ID_NONE && account_roster_manager_get(app->deps.roster, id, scratch) == 0) return scratch->label;
    return "";
}

void tui_app_refresh_contact_prefs(TuiApp *app) {
    ContactPanel *panel = &app->contact;
    AccountRosterManager *roster = app->deps.roster;
    if (!panel->open || !roster) return;
    ChatPrefs prefs;
    account_roster_manager_chat_prefs(roster, panel->jid, &prefs);
    Account scratch, in_view;
    char send_from[96] = "", merge[48] = "", agent[120] = "";
    if (tui_app_account_count(app) > 1) {
        const char *own = label_of(app, prefs.send_account, &scratch);
        if (own[0]) snprintf(send_from, sizeof(send_from), "%s", own);
        else snprintf(send_from, sizeof(send_from), "primary account (%s)", label_of(app, account_roster_manager_primary(roster), &scratch));
        str_copy(merge, sizeof(merge), prefs.merge == CHAT_MERGE_ALWAYS ? "always" : prefs.merge == CHAT_MERGE_NEVER ? "never"
                                     : current_settings(app)->merge_accounts ? "as the setting says (on)" : "as the setting says (off)");
    }
    if (account_roster_manager_get(roster, app->deps.active_account, &in_view) == 0) {
        char chats[1024];
        self_approval_chats(app, &in_view, chats, sizeof(chats));
        int on = jid_list_contains(chats, panel->jid);
        const char *who = tui_app_account_count(app) > 1 ? in_view.label : "";
        if (effective_access(app, &in_view) != ACCOUNT_AGENT_ADMIN) {
            snprintf(agent, sizeof(agent), "%s%s%s (needs agent access admin)", on ? "on" : "off", who[0] ? " for " : "", who);
        } else {
            snprintf(agent, sizeof(agent), "%s%s%s", on ? "\xE2\x9C\x93 on" : "off", who[0] ? " for " : "", who);
        }
    }
    contact_panel_set_prefs(panel, send_from, merge, agent);
    app->dirty = 1;
}

/* Primary, then each running account in turn, then primary again. */
void tui_app_step_send_from(TuiApp *app, const char *jid) {
    AccountRosterManager *roster = app->deps.roster;
    IAccountDirectory *dir = app->deps.directory;
    if (!roster || !dir) return;
    ChatPrefs prefs;
    account_roster_manager_chat_prefs(roster, jid, &prefs);
    int n = dir->count(dir), next = 0;
    for (int i = 0; prefs.send_account != ACCOUNT_ID_NONE && i < n; i++) {
        const AccountServices *sv = dir->at(dir, i);
        if (sv && sv->id == prefs.send_account) { next = i + 1; break; }
    }
    const AccountServices *sv = next < n ? dir->at(dir, next) : NULL;
    if (account_roster_manager_set_send_account(roster, jid, sv ? sv->id : ACCOUNT_ID_NONE) != 0) {
        tui_app_toast(app, account_roster_manager_error(roster), 1);
    }
    app->chat_rows_stale = 1;
    tui_app_refresh_contact_prefs(app);
}

void tui_app_step_merge(TuiApp *app, const char *jid) {
    AccountRosterManager *roster = app->deps.roster;
    if (!roster) return;
    ChatPrefs prefs;
    account_roster_manager_chat_prefs(roster, jid, &prefs);
    ChatMergeChoice next = prefs.merge == CHAT_MERGE_FOLLOW ? CHAT_MERGE_ALWAYS : prefs.merge == CHAT_MERGE_ALWAYS ? CHAT_MERGE_NEVER : CHAT_MERGE_FOLLOW;
    if (account_roster_manager_set_merge(roster, jid, next) != 0) tui_app_toast(app, account_roster_manager_error(roster), 1);
    app->chat_rows_stale = 1;
    tui_app_refresh_contact_prefs(app);
}

void tui_app_toggle_agent_answers(TuiApp *app, const char *jid) {
    AccountRosterManager *roster = app->deps.roster;
    Account in_view;
    if (!roster || account_roster_manager_get(roster, app->deps.active_account, &in_view) != 0) return;
    char chats[1024], changed[1024];
    self_approval_chats(app, &in_view, chats, sizeof(chats));
    if (jid_list_contains(chats, "*") && jid_list_contains(chats, jid)) {
        /* Every chat is switched on at once; one cannot be taken out of that from here. */
        tui_app_toast(app, "Agents answer in every chat of this account. Change that under Settings, Automation, Answering for itself.", 1);
        return;
    }
    int on = !jid_list_contains(chats, jid);
    if (jid_list_set(chats, jid, on, changed, sizeof(changed)) != 0) {
        tui_app_toast(app, "That is too many chats to keep; switch some off first", 1);
        return;
    }
    if (account_roster_manager_set_self_approval_chats(roster, in_view.id, changed) != 0) {
        tui_app_toast(app, account_roster_manager_error(roster), 1);
        return;
    }
    if (on && effective_access(app, &in_view) != ACCOUNT_AGENT_ADMIN) {
        tui_app_toast(app, "Switched on, but it only takes effect once this account's agent access is admin", 0);
    } else {
        tui_app_toast(app, on ? "\xF0\x9F\xA4\x96 An agent may answer here by itself" : "An agent now waits for you here", 0);
    }
    tui_app_refresh_contact_prefs(app);
}

const UnreadTally *tui_app_tally(TuiApp *app) {
    IAccountDirectory *dir = app->deps.directory;
    if (!dir || tui_app_account_count(app) <= 1) return messaging_manager_tally(app->deps.messaging);
    memset(&app->total_tally, 0, sizeof(app->total_tally));
    for (int i = 0; ; i++) {
        const AccountServices *sv = dir->at(dir, i);
        if (!sv) break;
        const UnreadTally *one = messaging_manager_tally(sv->messaging);
        for (int t = 0; t < MESSAGE_TYPE_COUNT; t++) app->total_tally.counts[t] += one->counts[t];
    }
    return &app->total_tally;
}

void tui_app_keep_send_account(TuiApp *app) {
    AccountRosterManager *roster = app->deps.roster;
    const char *jid = messaging_manager_open_jid(app->deps.messaging);
    if (!roster || !jid[0] || tui_app_account_count(app) <= 1) return;
    if (account_roster_manager_set_send_account(roster, jid, app->deps.active_account) != 0) {
        tui_app_toast(app, account_roster_manager_error(roster), 1);
        return;
    }
    Account account;
    char msg[ACCOUNT_LABEL_SIZE + 64];
    snprintf(msg, sizeof(msg), "This contact is always sent to from %s now",
             account_roster_manager_get(roster, app->deps.active_account, &account) == 0 ? account.label : "this account");
    tui_app_toast(app, msg, 0);
    app->chat_rows_stale = 1;
}
