/* The owner's chat: the "message yourself" chat of one of your numbers.
 * What you type there on your phone reaches a connected agent as your words,
 * the agent answers you there by itself, and a send that waits for your
 * answer in tawk is put to you there as a card you can allow or decline.
 * Your messages are told from tawk's own by what tawk sent: both carry your
 * number. */
#include "control_server_state.h"
#include "core/outgoing_text.h"
#include "engines/approval_card_text.h"
#include "engines/approval_reply_parser.h"
#include "engines/client_version.h"
#include "engines/owner_reply_rule.h"
#include "engines/remote_approval_policy.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

#define CARD_SIZE     4096
#define NO_AGENT_MS   60000     /* how often you are told that nobody hears you */

static int several(const ControlServer *s) { return s->deps.directory && s->deps.roster; }

static AccountId serving(const ControlServer *s) { return several(s) ? s->account : ACCOUNT_ID_FIRST; }

static AccountId owner_account(ControlServer *s) {
    return s->deps.owner ? owner_chat_manager_account(s->deps.owner) : ACCOUNT_ID_NONE;
}

/* Serves the owner's account, remembering in *back the one served until now. */
static int serve_owner(ControlServer *s, AccountId *back) {
    AccountId owner = owner_account(s);
    *back = s->account;
    if (owner == ACCOUNT_ID_NONE) return -1;
    if (!several(s)) return owner == ACCOUNT_ID_FIRST ? 0 : -1;
    return control_serve_account(s, owner);
}

static void serve_back(ControlServer *s, AccountId back) {
    if (several(s) && back != ACCOUNT_ID_NONE) control_serve_account(s, back);
}

int control_owner_here(ControlServer *s, const char *chat_jid) {
    if (!s->deps.owner || !chat_jid || !chat_jid[0]) return 0;
    return owner_chat_manager_is(s->deps.owner, serving(s), messaging_manager_user_jid(s->deps.messaging), chat_jid);
}

void control_owner_note_since(ControlServer *s, uint64_t before, SentKind kind, int ref) {
    if (!s->deps.owner) return;
    LiveMessageRef sent[8];
    int n = messaging_manager_live_since(s->deps.messaging, before, sent, 8);
    for (int i = 0; i < n; i++) {
        if (sent[i].kind != LIVE_KIND_MESSAGE || !control_owner_here(s, sent[i].chat_jid)) continue;
        owner_chat_manager_note_sent(s->deps.owner, serving(s), sent[i].id, kind, ref);
    }
}

int control_owner_reply(ControlServer *s, const ControlSession *session, const ControlPending *p) {
    if (session->origin != CONTROL_ORIGIN_MCP || p->new_chat || !owner_reply_rule_covers(p->op)) return 0;
    if (!control_owner_here(s, p->chat_jid)) return 0;
    return owner_chat_manager_take_reply(s->deps.owner, clock_now_ms());
}

/* Says something in the owner's chat, which must be the account being served. The id of the
 * message is written to `id` when there is room for it. */
static int say(ControlServer *s, const char *text, SentKind kind, int ref, char *id, size_t size) {
    if (id && size) id[0] = '\0';
    if (!control_connected(s)) return -1;
    const char *self = messaging_manager_user_jid(s->deps.messaging);
    if (!self || !self[0]) return -1;
    OutgoingText out;
    memset(&out, 0, sizeof(out));
    out.text = text;
    uint64_t before = messaging_manager_live_last(s->deps.messaging);
    if (messaging_manager_send_text_to(s->deps.messaging, self, &out) != 0) return -1;
    LiveMessageRef sent[4];
    int n = messaging_manager_live_since(s->deps.messaging, before, sent, 4);
    for (int i = 0; i < n; i++) {
        if (sent[i].kind != LIVE_KIND_MESSAGE) continue;
        owner_chat_manager_note_sent(s->deps.owner, serving(s), sent[i].id, kind, ref);
        if (id && size && !id[0]) str_copy(id, size, sent[i].id);
    }
    return 0;
}

/* A line of tawk's own to you, from whichever account is being served. */
static void tell(ControlServer *s, const char *text) {
    AccountId back;
    if (serve_owner(s, &back) == 0) say(s, text, SENT_KIND_NOTE, 0, NULL, 0);
    serve_back(s, back);
}

/* ---- requests put to you ---- */

/* Puts `p` to you as a card. Returns 0 when it went, or never will; -1 to try again. */
static int send_card(ControlServer *s, ControlPending *p, int changed, int64_t now) {
    AccountId back = s->account;
    char name[128] = "", label[ACCOUNT_LABEL_SIZE] = "";
    int offered = 0;
    if (p->account == ACCOUNT_ID_NONE || control_serve_account(s, p->account) == 0) {
        const Chat *chat = p->chat_jid[0] ? control_visible_chat(s, p->chat_jid) : NULL;
        if (chat) str_copy(name, sizeof(name), chat->name);
        control_account_label(s, label, sizeof(label));
        offered = remote_approval_policy_offers(p->kind, p->new_chat, chat != NULL, control_owner_here(s, p->chat_jid));
    }
    serve_back(s, back);
    if (!offered) return 0;
    ApprovalCard card = { p->client, p->action, name, label, p->text, p->editable, changed,
                          (int)((p->expires_ms - now + 59999) / 60000) };
    char text[CARD_SIZE];
    approval_card_text(&card, text, sizeof(text));
    int rc = -1;
    if (serve_owner(s, &back) == 0) rc = say(s, text, SENT_KIND_CARD, p->approval_id, p->card_id, sizeof(p->card_id));
    serve_back(s, back);
    return rc;
}

void control_owner_tick(ControlServer *s, int64_t now) {
    if (!s->deps.owner || owner_account(s) == ACCOUNT_ID_NONE) return;
    int64_t wait = owner_chat_manager_card_wait_ms(s->deps.owner);
    if (wait < 0) return;
    for (int i = 0; i < s->pending_count; i++) {
        ControlPending *p = &s->pending[i];
        if (p->carded || !remote_approval_policy_due(p->asked_ms, now, wait)) continue;
        if (send_card(s, p, 0, now) == 0) p->carded = 1;
    }
}

/* Your answer to the card of request `approval_id`. */
static void answer_card(ControlServer *s, int approval_id, ApprovalReply reply, const char *words) {
    if (reply == APPROVAL_REPLY_NONE) return;
    ControlPending *p = control_writes_pending(s, approval_id);
    if (!p) { tell(s, "tawk: that request is no longer waiting. Nothing was sent."); return; }
    if (reply == APPROVAL_REPLY_EDIT) {
        if (control_writes_retext(s, approval_id, words) != 0) {
            tell(s, "tawk: the words of that request cannot be changed. Reply to its card with y or n.");
            return;
        }
        p = control_writes_pending(s, approval_id);
        if (p && send_card(s, p, 1, clock_now_ms()) != 0) tell(s, "tawk: changed, but it could not be read back. Answer it in tawk.");
        return;
    }
    control_writes_answer(s, approval_id, reply == APPROVAL_REPLY_ALLOW);
    tell(s, reply == APPROVAL_REPLY_ALLOW ? "tawk: allowed." : "tawk: declined. Nothing was sent.");
}

/* ---- your words to the agent ---- */

/* The agent that hears you: your default agent when it can, else the one running the newest version. */
static ControlSession *listener(ControlServer *s) {
    ControlSession *best = NULL;
    for (int i = 0; i < s->session_count; i++) {
        ControlSession *c = &s->sessions[i];
        if (!c->greeted || c->origin != CONTROL_ORIGIN_MCP || c->paused || !c->can_owner) continue;
        if (control_default_agent_is(s, c)) return c;
        if (!best || client_version_compare(c->version, best->version) > 0) best = c;
    }
    return best;
}

static void hand_on(ControlServer *s, const Message *msg, int64_t now) {
    ControlSession *agent = listener(s);
    if (!agent) {
        if (now - s->owner_told_ms < NO_AGENT_MS) return;
        s->owner_told_ms = now;
        tell(s, "tawk: no agent that takes messages from this chat is connected, so nobody heard that.");
        return;
    }
    char sender[128];
    control_sender_name(s, msg, sender, sizeof(sender));
    const Chat *chat = control_visible_chat(s, msg->chat_jid);
    cJSON *evt = cJSON_CreateObject();
    cJSON *c = cJSON_AddObjectToObject(evt, "chat");
    cJSON_AddStringToObject(c, "jid", msg->chat_jid);
    cJSON_AddStringToObject(c, "name", chat ? chat->name : "");
    cJSON_AddItemToObject(evt, "message", control_codec_message(msg, sender));
    control_tag_account(s, evt);
    control_reply(s, agent->conn, control_codec_event("owner_message", evt));
    automation_manager_record(s->deps.automation, agent->origin, agent->client, "owner_message", msg->chat_jid,
                              "heard what you wrote in the owner's chat", AUTOMATION_OUTCOME_DONE);
    s->changed = 1;
}

int control_owner_on_message(ControlServer *s, const LiveMessageRef *ref) {
    if (!control_owner_here(s, ref->chat_jid)) return 0;
    Message msg;
    if (messaging_manager_get(s->deps.messaging, ref->id, &msg) != 0) return 0;
    int taken = 0;
    SentKind kind;
    int request = 0;
    AccountId account = serving(s);
    if (!msg.from_me || owner_chat_manager_sent(s->deps.owner, account, msg.id, NULL, NULL)) {
        /* tawk's own: an agent's answer, a card or a note. It is a message like any other to whoever follows the chat. */
    } else if (msg.quoted_id[0] && owner_chat_manager_sent(s->deps.owner, account, msg.quoted_id, &kind, &request) && kind == SENT_KIND_CARD) {
        answer_card(s, request, approval_reply_parse(msg.text), msg.text);
        taken = 1;
    } else if (owner_chat_manager_kind(s->deps.owner, account, &msg) == OWNER_MESSAGE_WORDS) {
        hand_on(s, &msg, clock_now_ms());
        taken = 1;
    }
    message_dispose(&msg);
    return taken;
}

void control_owner_on_reaction(ControlServer *s, const LiveMessageRef *ref) {
    if (!control_owner_here(s, ref->chat_jid)) return;
    SentKind kind;
    int request = 0;
    if (!owner_chat_manager_sent(s->deps.owner, serving(s), ref->id, &kind, &request) || kind != SENT_KIND_CARD) return;
    ApprovalReply reply = approval_reply_reaction(ref->detail);
    if (reply == APPROVAL_REPLY_ALLOW || reply == APPROVAL_REPLY_DECLINE) answer_card(s, request, reply, NULL);
}
