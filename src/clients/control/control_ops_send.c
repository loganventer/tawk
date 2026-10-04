/* Sending over the control socket: messages, reactions, messages for later,
 * read marks and drafts. */
#include "control_server_state.h"
#include "core/outgoing_text.h"
#include "core/quote_ref.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *checked_text(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const char *text = control_required(s, session, req, "text");
    if (text && strlen(text) > CONTROL_MAX_TEXT_BYTES) {
        control_fail(s, session->conn, req->id, "bad_request", "\"text\" is longer than 65536 bytes");
        return NULL;
    }
    return text;
}

static int resolve(ControlServer *s, ControlSession *session, const ControlRequest *req, char *jid, size_t size) {
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    int at = control_resolve_chat(s, session, req, "chat", all, n);
    if (at < 0) return -1;
    str_copy(jid, size, all[at].jid);
    return 0;
}

/* The chat a new message is for, served by the account you send to that contact from. */
static int resolve_recipient(ControlServer *s, ControlSession *session, const ControlRequest *req, char *jid, size_t size) {
    if (control_follow_sender(s, session, req) != 0) return -1;
    return resolve(s, session, req, jid, size);
}

/* ---- send_message ---- */

static cJSON *do_send(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    QuoteRef quote;
    memset(&quote, 0, sizeof(quote));
    const char *reply_to = control_codec_string(p->args, "reply_to");
    Message answered;
    int has_quote = reply_to && messaging_manager_get(s->deps.messaging, reply_to, &answered) == 0;
    if (has_quote) {
        str_copy(quote.id, sizeof(quote.id), answered.id);
        str_copy(quote.sender, sizeof(quote.sender), answered.from_me ? messaging_manager_user_jid(s->deps.messaging) : answered.sender_jid);
        str_copy(quote.text, sizeof(quote.text), answered.text ? answered.text : "");
        message_dispose(&answered);
    }
    OutgoingText out = { p->text, has_quote ? &quote : NULL, NULL, 0, 0, 0 };
    uint64_t before = messaging_manager_live_last(s->deps.messaging);
    if (messaging_manager_send_text_to(s->deps.messaging, p->chat_jid, &out) != 0) {
        str_copy(f->why, sizeof(f->why), "The message could not be queued");
        return NULL;
    }
    cJSON *r = cJSON_CreateObject();
    LiveMessageRef sent[4];
    int n = messaging_manager_live_since(s->deps.messaging, before, sent, 4);
    for (int i = 0; i < n; i++) {
        if (strcmp(sent[i].chat_jid, p->chat_jid) == 0) { cJSON_AddStringToObject(r, "id", sent[i].id); break; }
    }
    return r;
}

void control_op_send_message(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    char jid[128];
    if (resolve_recipient(s, session, req, jid, sizeof(jid)) != 0) return;
    const char *text = checked_text(s, session, req);
    if (!text) return;
    ControlPending p;
    control_pending_init(&p, req->id, "send_message", WRITE_KIND_SEND, do_send);
    str_copy(p.chat_jid, sizeof(p.chat_jid), jid);
    const char *reply_to = control_codec_string(req->args, "reply_to");
    if (reply_to && *reply_to) {
        Message answered;
        int ok = messaging_manager_get(s->deps.messaging, reply_to, &answered) == 0;
        int same_chat = ok && strcmp(answered.chat_jid, jid) == 0;
        if (ok) message_dispose(&answered);
        if (!same_chat) {
            control_pending_dispose(&p);
            control_fail(s, session->conn, req->id, "not_found", "\"reply_to\" is not a message in that chat");
            return;
        }
        cJSON_AddStringToObject(p.args, "reply_to", reply_to);
    }
    p.text = str_dup(text);
    p.editable = 1;
    p.needs_connection = 1;
    str_copy(p.action, sizeof(p.action), reply_to && *reply_to ? "send a reply" : "send a message");
    control_write(s, session, &p);
}

/* ---- react ---- */

static cJSON *do_react(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    const char *id = control_codec_string(p->args, "message_id");
    const char *emoji = control_codec_string(p->args, "emoji");
    if (messaging_manager_react(s->deps.messaging, id, emoji ? emoji : "") != 0) {
        str_copy(f->why, sizeof(f->why), "The reaction could not be sent");
        return NULL;
    }
    return cJSON_CreateObject();
}

void control_op_react(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const char *emoji = control_codec_string(req->args, "emoji");
    if (!emoji || strlen(emoji) > 31) {
        control_fail(s, session->conn, req->id, "bad_request", "\"emoji\" is required (an empty string removes your reaction)");
        return;
    }
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    ControlPending p;
    control_pending_init(&p, req->id, "react", WRITE_KIND_SEND, do_react);
    str_copy(p.chat_jid, sizeof(p.chat_jid), msg.chat_jid);
    cJSON_AddStringToObject(p.args, "message_id", msg.id);
    cJSON_AddStringToObject(p.args, "emoji", emoji);
    if (emoji[0]) snprintf(p.action, sizeof(p.action), "react with %s", emoji);
    else snprintf(p.action, sizeof(p.action), "remove your reaction");
    p.needs_connection = 1;
    message_dispose(&msg);
    control_write(s, session, &p);
}

/* ---- schedule_message ---- */

static cJSON *do_schedule(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    int64_t due = (int64_t)control_codec_int(p->args, "due_at", 0, 0, INT64_MAX), now = (int64_t)time(NULL);
    char id[64] = "";
    if (due <= now) { str_copy(f->why, sizeof(f->why), "That time has passed"); return NULL; }
    if (scheduling_manager_schedule(s->deps.scheduling, p->chat_jid, p->text, NULL, due, now, id, sizeof(id)) != 0) {
        str_copy(f->why, sizeof(f->why), scheduling_manager_error(s->deps.scheduling));
        return NULL;
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "id", id);
    cJSON_AddNumberToObject(r, "due_at", (double)due);
    return r;
}

void control_op_schedule_message(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    char jid[128];
    if (resolve_recipient(s, session, req, jid, sizeof(jid)) != 0) return;
    const char *text = checked_text(s, session, req);
    if (!text) return;
    int64_t due = 0;
    if (scheduling_manager_parse_when(s->deps.scheduling, control_codec_string(req->args, "when"), (int64_t)time(NULL), &due) != 0) {
        control_fail(s, session->conn, req->id, "bad_request", scheduling_manager_error(s->deps.scheduling));
        return;
    }
    ControlPending p;
    control_pending_init(&p, req->id, "schedule_message", WRITE_KIND_SEND, do_schedule);
    str_copy(p.chat_jid, sizeof(p.chat_jid), jid);
    cJSON_AddNumberToObject(p.args, "due_at", (double)due);
    p.text = str_dup(text);
    p.editable = 1;
    char when[48];
    clock_format_upcoming(due, control_settings(s)->use_24h_clock, when, sizeof(when));
    snprintf(p.action, sizeof(p.action), "send later, %s", when);
    control_write(s, session, &p);
}

/* ---- mark_read ---- */

static cJSON *do_mark_read(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    (void)f;
    messaging_manager_mark_read(s->deps.messaging, p->chat_jid);
    return cJSON_CreateObject();
}

void control_op_mark_read(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    char jid[128];
    if (resolve(s, session, req, jid, sizeof(jid)) != 0) return;
    ControlPending p;
    control_pending_init(&p, req->id, "mark_read", WRITE_KIND_SEND, do_mark_read);
    str_copy(p.chat_jid, sizeof(p.chat_jid), jid);
    str_copy(p.action, sizeof(p.action), "mark the chat as read");
    control_write(s, session, &p);
}

/* ---- draft_message: nothing is sent, so nobody is asked ---- */

void control_op_draft_message(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    char jid[128];
    if (resolve_recipient(s, session, req, jid, sizeof(jid)) != 0) return;
    const char *text = checked_text(s, session, req);
    if (!text) return;
    ControlPending p;
    control_pending_init(&p, req->id, "draft_message", WRITE_KIND_SEND, NULL);
    str_copy(p.chat_jid, sizeof(p.chat_jid), jid);
    p.origin = session->origin;
    str_copy(p.client, sizeof(p.client), session->client);
    if (automation_manager_check_write(s->deps.automation, CONTROL_ORIGIN_CLI, WRITE_KIND_SEND, clock_now_ms(), NULL) == AUTOMATION_VERDICT_REFUSE) {
        automation_manager_record(s->deps.automation, session->origin, session->client, "draft_message", jid, text, AUTOMATION_OUTCOME_REFUSED);
        control_fail(s, session->conn, req->id, "not_allowed", "Drafting needs access \"send\" in tawk (Settings > Automation > What they may do)");
    } else {
        int rc = messaging_manager_offer_draft(s->deps.messaging, jid, text);
        if (rc == 1) {
            control_fail(s, session->conn, req->id, "draft_exists", "That chat already has a draft waiting; it was left as it is");
        } else if (rc != 0) {
            control_fail(s, session->conn, req->id, "failed", "Another draft is still being handed over; try again in a moment");
        } else {
            automation_manager_record(s->deps.automation, session->origin, session->client, "draft_message", jid, text, AUTOMATION_OUTCOME_DONE);
            cJSON *r = cJSON_CreateObject();
            cJSON_AddBoolToObject(r, "drafted", 1);
            control_reply(s, session->conn, control_codec_ok(req->id, r));
            s->changed = 1;
        }
    }
    control_pending_dispose(&p);
}
