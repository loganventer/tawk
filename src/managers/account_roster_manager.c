#include "managers/account_roster_manager.h"
#include "engines/account_label_validator.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

struct AccountRosterManager {
    AccountRosterManagerDeps deps;
    char                     error[160];
    int                      changed;
};

static int refuse(AccountRosterManager *m, const char *why) {
    str_copy(m->error, sizeof(m->error), why);
    return -1;
}

static int changed(AccountRosterManager *m, int rc) {
    if (rc == 0) m->changed = 1;
    else if (!m->error[0]) str_copy(m->error, sizeof(m->error), "The change could not be saved.");
    return rc;
}

AccountRosterManager *account_roster_manager_create(const AccountRosterManagerDeps *deps) {
    AccountRosterManager *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->deps = *deps;
    return m;
}

void account_roster_manager_destroy(AccountRosterManager *mgr) {
    free(mgr);
}

int account_roster_manager_list(AccountRosterManager *mgr, Account *out, int max) {
    int n = mgr->deps.accounts->list(mgr->deps.accounts, out, max);
    return n < 0 ? 0 : n;
}

int account_roster_manager_get(AccountRosterManager *mgr, AccountId id, Account *out) {
    return mgr->deps.accounts->get(mgr->deps.accounts, id, out);
}

AccountId account_roster_manager_primary(AccountRosterManager *mgr) {
    Account all[ACCOUNT_MAX];
    int n = account_roster_manager_list(mgr, all, ACCOUNT_MAX);
    for (int i = 0; i < n; i++) if (all[i].is_primary) return all[i].id;
    return n > 0 ? all[0].id : ACCOUNT_ID_NONE;
}

/* The label as it is kept: without the spaces around it. */
static void tidy_label(const char *label, char *out, size_t size) {
    char copy[ACCOUNT_LABEL_SIZE * 2];
    str_copy(copy, sizeof(copy), label ? label : "");
    str_copy(out, size, str_trim(copy));
}

/* 0 when `label` is fit to use and no account but `self` has it. */
static int check_label(AccountRosterManager *m, const char *label, AccountId self) {
    char why[160];
    if (account_label_validate(label, why, sizeof(why)) != 0) return refuse(m, why);
    Account all[ACCOUNT_MAX];
    int n = account_roster_manager_list(m, all, ACCOUNT_MAX);
    for (int i = 0; i < n; i++) {
        if (all[i].id != self && account_label_same(all[i].label, label)) return refuse(m, "Another account already has that label.");
    }
    return 0;
}

/* The first badge colour no account uses yet. */
static int free_colour(const Account *all, int n) {
    for (int colour = 0; colour < ACCOUNT_COLOURS; colour++) {
        int used = 0;
        for (int i = 0; i < n && !used; i++) used = all[i].colour == colour;
        if (!used) return colour;
    }
    return n % ACCOUNT_COLOURS;
}

int account_roster_manager_add(AccountRosterManager *mgr, const char *label, int64_t now, AccountId *id_out) {
    mgr->error[0] = '\0';
    char tidy[ACCOUNT_LABEL_SIZE];
    tidy_label(label, tidy, sizeof(tidy));
    if (check_label(mgr, tidy, ACCOUNT_ID_NONE) != 0) return -1;
    Account all[ACCOUNT_MAX];
    int n = account_roster_manager_list(mgr, all, ACCOUNT_MAX);
    if (n >= ACCOUNT_MAX) return refuse(mgr, "That is as many accounts as tawk holds.");
    AccountId id = ACCOUNT_ID_NONE;
    if (changed(mgr, mgr->deps.accounts->add(mgr->deps.accounts, tidy, free_colour(all, n), now, &id)) != 0) return -1;
    if (n == 0) mgr->deps.accounts->set_primary(mgr->deps.accounts, id);     /* the only account is the primary one */
    if (id_out) *id_out = id;
    return 0;
}

int account_roster_manager_rename(AccountRosterManager *mgr, AccountId id, const char *label) {
    mgr->error[0] = '\0';
    char tidy[ACCOUNT_LABEL_SIZE];
    tidy_label(label, tidy, sizeof(tidy));
    if (check_label(mgr, tidy, id) != 0) return -1;
    return changed(mgr, mgr->deps.accounts->rename(mgr->deps.accounts, id, tidy));
}

int account_roster_manager_set_primary(AccountRosterManager *mgr, AccountId id) {
    mgr->error[0] = '\0';
    return changed(mgr, mgr->deps.accounts->set_primary(mgr->deps.accounts, id));
}

int account_roster_manager_set_agent_access(AccountRosterManager *mgr, AccountId id, AccountAgentAccess access) {
    mgr->error[0] = '\0';
    if (access < 0 || access >= ACCOUNT_AGENT_ACCESS_COUNT) return refuse(mgr, "That is not an access level.");
    return changed(mgr, mgr->deps.accounts->set_agent_access(mgr->deps.accounts, id, access));
}

int account_roster_manager_remove(AccountRosterManager *mgr, AccountId id) {
    mgr->error[0] = '\0';
    Account all[ACCOUNT_MAX];
    int n = account_roster_manager_list(mgr, all, ACCOUNT_MAX);
    const Account *gone = NULL, *heir = NULL;
    for (int i = 0; i < n; i++) {
        if (all[i].id == id) gone = &all[i];
        else if (!heir) heir = &all[i];                 /* the oldest account left */
    }
    if (!gone) return refuse(mgr, "There is no such account.");
    if (!heir) return refuse(mgr, "The last account cannot be removed. Log it out instead.");
    if (changed(mgr, mgr->deps.accounts->remove(mgr->deps.accounts, id)) != 0) return -1;
    if (gone->is_primary) mgr->deps.accounts->set_primary(mgr->deps.accounts, heir->id);
    mgr->deps.prefs->forget_account(mgr->deps.prefs, id);
    return 0;
}

const char *account_roster_manager_error(const AccountRosterManager *mgr) {
    return mgr->error;
}

int account_roster_manager_linked(AccountRosterManager *mgr, AccountId id, const char *jid, const char *name) {
    Account now;
    if (mgr->deps.accounts->get(mgr->deps.accounts, id, &now) != 0) return -1;
    if (strcmp(now.jid, jid ? jid : "") == 0 && (!name || !name[0] || strcmp(now.name, name) == 0)) return 0;   /* nothing new */
    return changed(mgr, mgr->deps.accounts->set_identity(mgr->deps.accounts, id, jid, name));
}

int account_roster_manager_set_last_chat(AccountRosterManager *mgr, AccountId id, const char *jid) {
    return mgr->deps.accounts->set_last_chat(mgr->deps.accounts, id, jid);
}

int account_roster_manager_last_chat(AccountRosterManager *mgr, AccountId id, char *out, size_t size) {
    return mgr->deps.accounts->get_last_chat(mgr->deps.accounts, id, out, size);
}

int account_roster_manager_set_self_approval_chats(AccountRosterManager *mgr, AccountId id, const char *chats) {
    mgr->error[0] = '\0';
    return changed(mgr, mgr->deps.accounts->set_self_approval_chats(mgr->deps.accounts, id, chats));
}

int account_roster_manager_self_approval_chats(AccountRosterManager *mgr, AccountId id, char *out, size_t size) {
    return mgr->deps.accounts->get_self_approval_chats(mgr->deps.accounts, id, out, size);
}

int account_roster_manager_chat_prefs(AccountRosterManager *mgr, const char *jid, ChatPrefs *out) {
    return mgr->deps.prefs->get(mgr->deps.prefs, jid, out);
}

int account_roster_manager_set_send_account(AccountRosterManager *mgr, const char *jid, AccountId account) {
    mgr->error[0] = '\0';
    Account found;
    if (account != ACCOUNT_ID_NONE && mgr->deps.accounts->get(mgr->deps.accounts, account, &found) != 0) {
        return refuse(mgr, "There is no such account.");
    }
    return changed(mgr, mgr->deps.prefs->set_send_account(mgr->deps.prefs, jid, account));
}

int account_roster_manager_set_merge(AccountRosterManager *mgr, const char *jid, ChatMergeChoice merge) {
    mgr->error[0] = '\0';
    if (merge != CHAT_MERGE_FOLLOW && merge != CHAT_MERGE_ALWAYS && merge != CHAT_MERGE_NEVER) return refuse(mgr, "That is not a choice.");
    return changed(mgr, mgr->deps.prefs->set_merge(mgr->deps.prefs, jid, merge));
}

int account_roster_manager_send_accounts(AccountRosterManager *mgr, ChatPrefs *out, int max) {
    int n = mgr->deps.prefs->list_send_accounts(mgr->deps.prefs, out, max);
    return n < 0 ? 0 : n;
}

int account_roster_manager_take_changed(AccountRosterManager *mgr) {
    int was = mgr->changed;
    mgr->changed = 0;
    return was;
}
