/* Which of your accounts a request is for. Each account has its own access
 * level for agents, and one that is off is neither listed nor reachable:
 * naming it reads the same as naming an account that does not exist. */
#include "control_server_state.h"
#include "engines/account_agent_policy.h"
#include "engines/account_label_validator.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

static int several(ControlServer *s) { return s->deps.directory && s->deps.roster; }

/* The accounts agents may use, in id order. Returns how many. */
static int visible_accounts(ControlServer *s, Account *out) {
    Account all[ACCOUNT_MAX];
    int n = account_roster_manager_list(s->deps.roster, all, ACCOUNT_MAX), kept = 0;
    for (int i = 0; i < n; i++) {
        if (!account_agent_policy_visible(&all[i], control_settings(s))) continue;
        if (!s->deps.directory->find(s->deps.directory, all[i].id)) continue;     /* not running */
        out[kept++] = all[i];
    }
    return kept;
}

int control_account_count(ControlServer *s) {
    if (!several(s)) return 1;
    Account seen[ACCOUNT_MAX];
    return visible_accounts(s, seen);
}

AccountId control_account_at(ControlServer *s, int index) {
    if (!several(s)) return index == 0 ? ACCOUNT_ID_FIRST : ACCOUNT_ID_NONE;
    Account seen[ACCOUNT_MAX];
    int n = visible_accounts(s, seen);
    return index >= 0 && index < n ? seen[index].id : ACCOUNT_ID_NONE;
}

void control_account_label(ControlServer *s, char *out, size_t size) {
    out[0] = '\0';
    Account account;
    if (control_account_count(s) < 2 || account_roster_manager_get(s->deps.roster, s->account, &account) != 0) return;
    str_copy(out, size, account.label);
}

int control_any_admin(ControlServer *s) {
    if (!several(s)) return strcmp(control_settings(s)->automation_access, "admin") == 0;
    Account seen[ACCOUNT_MAX];
    int n = visible_accounts(s, seen);
    for (int i = 0; i < n; i++) {
        if (account_agent_policy_access(&seen[i], control_settings(s)) == ACCOUNT_AGENT_ADMIN) return 1;
    }
    return 0;
}

AccountId control_default_account(ControlServer *s) {
    if (!several(s)) return ACCOUNT_ID_FIRST;
    Account seen[ACCOUNT_MAX];
    int n = visible_accounts(s, seen);
    for (int i = 0; i < n; i++) if (seen[i].is_primary) return seen[i].id;
    return n > 0 ? seen[0].id : ACCOUNT_ID_NONE;
}

int control_serve_account(ControlServer *s, AccountId id) {
    if (!several(s)) return 0;                              /* the one account, served as it always was */
    Account account;
    if (account_roster_manager_get(s->deps.roster, id, &account) != 0) return -1;
    const Settings *settings = control_settings(s);
    if (!account_agent_policy_visible(&account, settings)) return -1;
    const AccountServices *sv = s->deps.directory->find(s->deps.directory, id);
    if (!sv) return -1;
    s->deps.messaging = sv->messaging;
    s->deps.profiles = sv->profiles;
    s->deps.scheduling = sv->scheduling;
    s->deps.feed = sv->feed;
    s->deps.accounts = sv->accounts;
    s->deps.statuses = sv->statuses;
    s->deps.calls = sv->calls;
    s->deps.backend_name = sv->backend_name;
    s->account = id;
    char own[1024];
    account_roster_manager_self_approval_chats(s->deps.roster, id, own, sizeof(own));
    automation_manager_serve(s->deps.automation, id, account_agent_access_name(account_agent_policy_access(&account, settings)),
                             account_agent_policy_self_chats(&account, own, settings));
    return 0;
}

/* The account a reference names: its id as a number, or its label. */
static AccountId named(ControlServer *s, const cJSON *ref) {
    Account seen[ACCOUNT_MAX];
    int n = visible_accounts(s, seen);
    for (int i = 0; i < n; i++) {
        if (cJSON_IsNumber(ref) && seen[i].id == ref->valueint) return seen[i].id;
        if (cJSON_IsString(ref) && ref->valuestring) {
            char *end = NULL;
            long number = strtol(ref->valuestring, &end, 10);
            if (end && *end == '\0' && end != ref->valuestring && seen[i].id == number) return seen[i].id;
            if (account_label_same(seen[i].label, ref->valuestring)) return seen[i].id;
        }
    }
    return ACCOUNT_ID_NONE;
}

int control_request_account(ControlServer *s, const ControlSession *session, const ControlRequest *req) {
    if (!several(s)) return 0;
    const cJSON *ref = req->args ? cJSON_GetObjectItemCaseSensitive(req->args, "account") : NULL;
    AccountId id = ref && !cJSON_IsNull(ref) ? named(s, ref) : control_default_account(s);
    if (id == ACCOUNT_ID_NONE || control_serve_account(s, id) != 0) {
        /* The same answer whether it is off or does not exist: an agent learns nothing about an account it may not use. */
        control_fail(s, session->conn, req->id, ref ? "not_found" : "not_allowed",
                     ref ? "No such account" : "No account is open to agents (Settings, Account, Accounts)");
        return -1;
    }
    return 0;
}

static cJSON *account_json(ControlServer *s, const Account *account) {
    const AccountServices *sv = s->deps.directory->find(s->deps.directory, account->id);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", account->id);
    cJSON_AddStringToObject(o, "label", account->label);
    cJSON_AddStringToObject(o, "jid", sv ? messaging_manager_user_jid(sv->messaging) : account->jid);
    cJSON_AddStringToObject(o, "name", sv ? messaging_manager_user_name(sv->messaging) : account->name);
    cJSON_AddBoolToObject(o, "connected", sv && messaging_manager_auth_state(sv->messaging) == AUTH_STATE_CONNECTED);
    cJSON_AddBoolToObject(o, "primary", account->is_primary);
    cJSON_AddStringToObject(o, "access", account_agent_access_name(account_agent_policy_access(account, control_settings(s))));
    return o;
}

cJSON *control_accounts_json(ControlServer *s) {
    cJSON *list = cJSON_CreateArray();
    if (!several(s)) return list;
    Account seen[ACCOUNT_MAX];
    int n = visible_accounts(s, seen);
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(list, account_json(s, &seen[i]));
    return list;
}

void control_tag_account(ControlServer *s, cJSON *object) {
    if (!several(s) || !object || s->account == ACCOUNT_ID_NONE) return;
    Account account;
    if (account_roster_manager_get(s->deps.roster, s->account, &account) != 0) return;
    cJSON *o = cJSON_AddObjectToObject(object, "account");
    cJSON_AddNumberToObject(o, "id", account.id);
    cJSON_AddStringToObject(o, "label", account.label);
}

/* {"accounts":[{id,label,jid,name,connected,primary,access}],"default":id}: the accounts this client may use. */
void control_op_list_accounts(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "accounts", control_accounts_json(s));
    cJSON_AddNumberToObject(r, "default", control_default_account(s));
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}
