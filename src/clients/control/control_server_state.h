/* The control client's state and the operations shared between its files.
 * Private to src/clients/control. */
#ifndef APP_CLIENTS_CONTROL_CONTROL_SERVER_STATE_H
#define APP_CLIENTS_CONTROL_CONTROL_SERVER_STATE_H

#include <stdint.h>

#include "cJSON.h"
#include "clients/control/control_codec.h"
#include "clients/control/control_confirmation.h"
#include "clients/control/control_op.h"
#include "clients/control/control_pending.h"
#include "clients/control/control_request.h"
#include "clients/control/control_server.h"
#include "clients/control/control_session.h"

#define CONTROL_MAX_SESSIONS      16
#define CONTROL_MAX_PENDING       32
#define CONTROL_MAX_CONFIRMATIONS 16
#define CONTROL_PROTOCOL          1
#define CONTROL_CONFIRM_MS        (5 * 60 * 1000)
#define CONTROL_MAX_TEXT_BYTES    65536

struct ControlServer {
    ControlServerDeps   deps;
    IFrameHook          hook;
    ControlSession      sessions[CONTROL_MAX_SESSIONS];
    int                 session_count;
    ControlPending      pending[CONTROL_MAX_PENDING];        /* waiting for your answer */
    int                 pending_count;
    ControlConfirmation confirmations[CONTROL_MAX_CONFIRMATIONS];
    int                 confirmation_count;
    int                 next_approval;
    int                 listening;
    char                error[160];
    int64_t             next_listen_ms;       /* when to try listening again */
    int64_t             next_check_ms;        /* when to look at the socket file and unread counts */
    uint64_t            live_seq;             /* the newest message already sent to subscribers, with one account */
    /* With several accounts: the account being served now, and how far each one's live messages were sent. */
    AccountId           account;
    AccountId           live_accounts[ACCOUNT_MAX];
    uint64_t            live_seqs[ACCOUNT_MAX];
    int                 changed;
    /* TL;DR: the question out on WhatsApp about which agent writes summaries, and the agents it listed. */
    int                 summary_asking;
    AccountId           summary_ask_account;
    int                 summary_ask_conns[AUTOMATION_STATUS_SESSIONS];
    int                 summary_ask_count;
    int64_t             summary_asked_ms;
    /* Requests for summaries go out a few at once and then one every so often, so a chat's older messages do not flood an agent. */
    int                 summary_tokens;
    int64_t             summary_refill_ms;
    int64_t             owner_told_ms;        /* when you were last told that no agent hears the owner's chat */
};

/* ---- accounts (control_accounts.c) ---- */
/* Serves the account a request names in "account" (an id or a label), or the
 * default one, answering the client itself when it names one it may not use.
 * Returns 0 once that account's managers and rules are the ones in force. */
int   control_request_account(ControlServer *server, const ControlSession *session, const ControlRequest *req);
/* Serves the account `id`; -1 when it is not running or closed to agents. */
int   control_serve_account(ControlServer *server, AccountId id);
/* The account served when a request names none: the primary one if agents may use it, else the first they may. */
AccountId control_default_account(ControlServer *server);
/* The accounts agents may use, as hello and list_accounts give them. */
cJSON *control_accounts_json(ControlServer *server);
/* Marks an event or an answer with the account being served, when there is more than one. */
void  control_tag_account(ControlServer *server, cJSON *object);
/* How many accounts agents may use, and the n-th of them. */
int   control_account_count(ControlServer *server);
AccountId control_account_at(ControlServer *server, int index);
/* The label of the account being served, or "" while agents may use one account only. */
void  control_account_label(ControlServer *server, char *out, size_t size);
/* Whether any account lets an agent answer its own requests. */
int   control_any_admin(ControlServer *server);
void  control_op_list_accounts(ControlServer *server, ControlSession *session, const ControlRequest *req);

/* ---- the sending number (control_sender.c) ---- */
/* For a request that starts a new message and names no account: serves the
 * account you send to that contact from. Returns -1, having answered, when
 * that account is closed to agents. */
int   control_follow_sender(ControlServer *server, const ControlSession *session, const ControlRequest *req);

/* ---- helpers (control_server.c) ---- */
const Settings *control_settings(ControlServer *server);
ControlSession *control_session_of(ControlServer *server, int conn);
void            control_reply(ControlServer *server, int conn, char *line);   /* frees line */
void            control_fail(ControlServer *server, int conn, const char *id, const char *code, const char *message);
/* The chat named by args[`name`], answering the client itself when it is
 * missing or not found; returns its index in `chats`, or -1. */
int             control_resolve_chat(ControlServer *server, const ControlSession *session, const ControlRequest *req,
                                     const char *name, const Chat *chats, int count);
/* Who a new message is for, named by args[`name`]: a chat as above, or, when
 * no chat matches, a person named by phone number, JID or contact name who
 * has no chat yet (control_recipient.c). Writes their JID and whether the
 * message would start the chat; returns -1, having answered, otherwise. */
int             control_resolve_recipient(ControlServer *server, const ControlSession *session, const ControlRequest *req,
                                          const char *name, char *jid, size_t size, int *new_chat);
/* The visible chat a JID belongs to, or NULL. */
const Chat     *control_visible_chat(ControlServer *server, const char *jid);
/* A message by id, answering not_found itself unless its chat is visible. */
int             control_load_message(ControlServer *server, const ControlSession *session, const ControlRequest *req,
                                     const char *name, Message *out);
/* A required string argument, answering bad_request itself when missing. */
const char     *control_required(ControlServer *server, const ControlSession *session, const ControlRequest *req, const char *name);
void            control_sender_name(ControlServer *server, const Message *msg, char *out, unsigned long size);
/* control_presence.c: tells agents that the person in `chat` came online or left. */
void            control_presence_send(ControlServer *server, const Chat *chat, const LiveMessageRef *ref);
int             control_connected(ControlServer *server);

/* ---- writes (control_writes.c): checks, confirmation, approval ---- */
/* Takes `pending`: carries it out now, holds it for confirmation, asks
 * you, or refuses it, and answers the client in each case. */
void control_write(ControlServer *server, ControlSession *session, ControlPending *pending);
void control_op_confirm(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_cancel_confirmation(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* Access admin: a client answers its own waiting request, showing the admin token. */
void control_op_approve(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* Your answer to a waiting request, given somewhere other than tawk's own window: 0 when it was waiting. */
int  control_writes_answer(ControlServer *server, int approval_id, int approved);
/* Changes the words of a waiting request, as you may in tawk's window: 0 when it was waiting and may be changed. */
int  control_writes_retext(ControlServer *server, int approval_id, const char *text);
/* The waiting request with that approval id, or NULL. */
ControlPending *control_writes_pending(ControlServer *server, int approval_id);
void control_writes_tick(ControlServer *server, int64_t now_ms);
void control_writes_forget(ControlServer *server, int conn);

/* ---- operations, one group per file ---- */
/* control_ops_read.c */
void control_op_list_chats(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_read_messages(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_search_messages(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_unread_summary(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_chat_info(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_presence(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_list_statuses(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_list_scheduled(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* control_ops_send.c */
void control_op_send_message(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_react(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_schedule_message(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_mark_read(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_draft_message(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* control_ops_messages.c */
void control_op_edit_message(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_delete_message(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_forward_message(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_retry_message(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_download_media(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* control_ops_chats.c */
void control_op_set_chat(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_set_chat_theme(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_clear_chat(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_delete_chat(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_export_chat(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_block(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_unblock(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* control_ops_scheduled.c */
void control_op_cancel_scheduled(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_reschedule(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_send_scheduled_now(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* control_ops_statuses.c */
void control_op_status_viewers(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_list_backgrounds(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_post_status(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_reply_status(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_like_status(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* control_ops_profile.c */
void control_op_get_profile(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_set_profile(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_set_profile_photo(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_remove_profile_photo(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* control_ops_app.c */
void control_op_get_settings(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_set_setting(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_list_themes(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_app_status(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_describe(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_reconnect(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_decline_call(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* control_ops_transcripts.c */
void control_op_set_transcript(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_get_transcript(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* Tells one agent about older voice notes that wait for a transcript. */
void control_transcripts_tick(ControlServer *server);
/* Adds "transcribe":false to an answer or event about `chat` when its voice notes are not to be
 * transcribed, and "languages" when you said which languages they are spoken in. */
void control_tag_transcribe(ControlServer *server, cJSON *object, const Chat *chat);
/* control_ops_summaries.c */
void control_op_set_summary(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_get_summary(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* Adds "tldr":true to an answer about `chat` when it is in TL;DR mode. */
void control_tag_tldr(ControlServer *server, cJSON *object, const Chat *chat);
/* control_summariser.c: which agent writes TL;DR summaries, and telling it which messages wait for one. */
/* `session` handed back the summary of `message_id`. */
void control_summary_answered(ControlSession *session, const char *message_id);
/* Whether `session` is the one you chose. */
int  control_summariser_is(ControlServer *server, const ControlSession *session);
/* You chose the agent on `conn` in the Agents tab; choosing it again takes the choice back. */
void control_summariser_choose(ControlServer *server, int conn);
/* A message arrived in the account being served: it may be one to summarise, or your answer to
 * tawk's question, in which case 1 is returned and it is nothing else. */
int  control_summaries_on_message(ControlServer *server, const LiveMessageRef *ref);
/* Whether `session` is the agent you chose as your default. */
int  control_default_agent_is(ControlServer *server, const ControlSession *session);
/* control_owner.c: the owner's chat, where you and the agent talk on WhatsApp. */
/* Whether `chat_jid`, in the account being served, is the owner's chat. */
int  control_owner_here(ControlServer *server, const char *chat_jid);
/* Whether a write is an agent's answer to you there that may go out unasked; a yes counts against this hour. */
int  control_owner_reply(ControlServer *server, const ControlSession *session, const ControlPending *pending);
/* Remembers the messages the account being served put into the owner's chat since `before`. */
void control_owner_note_since(ControlServer *server, uint64_t before, SentKind kind, int ref);
/* A message in the account being served: 1 when it was your words to the agent, or your answer to a request. */
int  control_owner_on_message(ControlServer *server, const LiveMessageRef *ref);
/* A reaction in the account being served: a thumb on a request's card answers it. */
void control_owner_on_reaction(ControlServer *server, const LiveMessageRef *ref);
/* Puts the requests that have waited long enough to you in the owner's chat. */
void control_owner_tick(ControlServer *server, int64_t now_ms);
/* Hands waiting messages to the agent that writes summaries, or asks you which agent that is. */
void control_summaries_tick(ControlServer *server, int64_t now_ms);
/* control_ops_live.c */
void control_op_subscribe(ControlServer *server, ControlSession *session, const ControlRequest *req);
void control_op_unsubscribe(ControlServer *server, ControlSession *session, const ControlRequest *req);
/* New messages and changed unread counts to subscribers. */
void control_live_tick(ControlServer *server, int check_unread);

#endif
