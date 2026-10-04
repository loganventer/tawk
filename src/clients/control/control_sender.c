/* Which of your accounts a new message leaves from when the request names
 * none: the one you chose for that contact, by the rule the chat list uses
 * (engines/reply_account_policy.h). A number you closed to agents is never
 * swapped for another: the request is refused. */
#include "control_server_state.h"
#include "core/chat_prefs.h"
#include "engines/reply_account_policy.h"
#include "utilities/str_util.h"

#include <string.h>

/* Whether the account `id` has the chat `ref` names. The first account that has it settles which chat is meant. */
static int has_chat(ControlServer *s, AccountId id, const char *ref, char *jid, size_t size, int64_t *newest) {
    if (control_serve_account(s, id) != 0) return 0;
    int n = 0, found = -1;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    if (automation_manager_resolve(s->deps.automation, all, n, ref, &found, NULL, 0, NULL) != CHAT_RESOLUTION_FOUND) return 0;
    if (!jid[0]) str_copy(jid, size, all[found].jid);
    if (strcmp(jid, all[found].jid) != 0) return 0;
    *newest = all[found].last_ts;
    return 1;
}

/* Whether the account being served holds the message a reply quotes. WhatsApp quotes a message only in its own chat. */
static int holds(ControlServer *s, const char *message_id, const char *jid) {
    Message quoted;
    if (!message_id || !*message_id || messaging_manager_get(s->deps.messaging, message_id, &quoted) != 0) return 0;
    int here = strcmp(quoted.chat_jid, jid) == 0;
    message_dispose(&quoted);
    return here;
}

static int among(const ReplyAccountCandidate *candidates, int count, AccountId id) {
    for (int i = 0; i < count; i++) if (candidates[i].id == id) return 1;
    return 0;
}

int control_follow_sender(ControlServer *s, const ControlSession *session, const ControlRequest *req) {
    if (!s->deps.directory || !s->deps.roster) return 0;
    const cJSON *named = req->args ? cJSON_GetObjectItemCaseSensitive(req->args, "account") : NULL;
    const char *ref = control_codec_string(req->args, "chat");
    if ((named && !cJSON_IsNull(named)) || !ref || !*ref) return 0;

    AccountId usual = s->account;                          /* the default one, already being served */
    AccountId quoted = ACCOUNT_ID_NONE;                    /* the one that holds the message being answered */
    const char *reply_to = control_codec_string(req->args, "reply_to");
    ReplyAccountCandidate candidates[ACCOUNT_MAX];
    int n = 0, open = control_account_count(s);
    char jid[128] = "";
    candidates[n].id = usual;
    candidates[n].newest_ts = 0;
    candidates[n].has_chat = has_chat(s, usual, ref, jid, sizeof(jid), &candidates[n].newest_ts);
    if (candidates[n].has_chat && holds(s, reply_to, jid)) quoted = usual;
    n++;
    for (int i = 0; i < open && n < ACCOUNT_MAX; i++) {
        AccountId id = control_account_at(s, i);
        if (id == usual || id == ACCOUNT_ID_NONE) continue;
        candidates[n].id = id;
        candidates[n].newest_ts = 0;
        candidates[n].has_chat = has_chat(s, id, ref, jid, sizeof(jid), &candidates[n].newest_ts);
        if (candidates[n].has_chat && quoted == ACCOUNT_ID_NONE && holds(s, reply_to, jid)) quoted = id;
        n++;
    }
    control_serve_account(s, usual);
    if (!jid[0]) return 0;                                 /* no account has it: the operation says so itself */

    ChatPrefs prefs;
    Account chosen;
    if (account_roster_manager_chat_prefs(s->deps.roster, jid, &prefs) != 0) prefs.send_account = ACCOUNT_ID_NONE;
    if (quoted == ACCOUNT_ID_NONE && prefs.send_account != ACCOUNT_ID_NONE && !among(candidates, n, prefs.send_account) &&
        account_roster_manager_get(s->deps.roster, prefs.send_account, &chosen) == 0) {
        control_fail(s, session->conn, req->id, "not_allowed",
                     "You send to this contact from an account that is closed to agents (Settings, Account, Accounts)");
        return -1;
    }
    AccountId sender = reply_account_policy_choose(quoted, prefs.send_account, usual, candidates, n);
    if (sender != ACCOUNT_ID_NONE && sender != usual) control_serve_account(s, sender);
    return 0;
}
