/* Every write over the control socket passes through here: the automation
 * manager decides whether it may go ahead, destructive ones wait for their
 * client to confirm them, and those that need you wait for your answer in
 * tawk. Every outcome goes to the automation log. */
#include "control_server_state.h"
#include "engines/ai_disclaimer.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void summary_of(const ControlPending *p, char *out, size_t size) {
    if (p->text && *p->text) str_copy(out, size, p->text);
    else str_copy(out, size, p->action);
}

static void record(ControlServer *s, const ControlPending *p, AutomationOutcome outcome) {
    char summary[256];
    summary_of(p, summary, sizeof(summary));
    automation_manager_record(s->deps.automation, p->origin, p->client, p->op, p->chat_jid, summary, outcome);
    s->changed = 1;
}

/* A session's "allow for this session" is kept for a chat of one account: the same
 * contact on another account is asked about again. */
static void allowance_key(const ControlPending *p, char *out, size_t size) {
    if (p->account > ACCOUNT_ID_FIRST && p->chat_jid[0]) snprintf(out, size, "%d/%s", p->account, p->chat_jid);
    else str_copy(out, size, p->chat_jid);
}

static void finish(ControlServer *s, const ControlPending *p, AutomationOutcome done) {
    ControlFailure failure = { "failed", "" };
    /* It may have waited while other requests were served: back to the account it was asked of. */
    if (p->account != ACCOUNT_ID_NONE && control_serve_account(s, p->account) != 0) {
        failure.code = "not_allowed";
        str_copy(failure.why, sizeof(failure.why), "That account is no longer open to agents");
    } else if (p->needs_connection && !control_connected(s)) {
        failure.code = "offline";
        str_copy(failure.why, sizeof(failure.why), "tawk is not connected to WhatsApp right now");
    }
    uint64_t before = messaging_manager_live_last(s->deps.messaging);
    cJSON *r = failure.why[0] ? NULL : p->execute(s, p, &failure);
    /* Whatever a client puts into the owner's chat is remembered as tawk's, so it is never read back as your words. */
    if (r && control_owner_here(s, p->chat_jid)) control_owner_note_since(s, before, SENT_KIND_REPLY, 0);
    if (!r) {
        record(s, p, AUTOMATION_OUTCOME_FAILED);
        control_fail(s, p->conn, p->request_id, failure.code ? failure.code : "failed", failure.why[0] ? failure.why : "It could not be done");
        return;
    }
    if (p->edited || p->disclaimed) {
        if (p->edited) cJSON_AddBoolToObject(r, "edited", 1);
        if (p->disclaimed) cJSON_AddBoolToObject(r, "disclaimer", 1);
        cJSON_AddStringToObject(r, "text", p->text ? p->text : "");
    }
    record(s, p, done);
    control_tag_account(s, r);
    control_reply(s, p->conn, control_codec_ok(p->request_id, r));
}

/* Carries a write out, first adding the AI disclaimer under its words where the settings ask for one.
 * It is added last, after any edit of yours, so what you approved is what goes above it. */
static void carry_out(ControlServer *s, ControlPending *p, AutomationOutcome done) {
    char *with = ai_disclaimer_append(p->text, automation_manager_disclaimer(s->deps.automation, p->origin, p->op));
    if (with) {
        free(p->text);
        p->text = with;
        p->disclaimed = 1;
    }
    finish(s, p, done);
}

static void chat_name(ControlServer *s, const char *jid, char *out, size_t size) {
    out[0] = '\0';
    if (!jid[0]) return;
    const Chat *c = control_visible_chat(s, jid);
    if (c) str_copy(out, size, c->name);
    else messaging_manager_display_name(s->deps.messaging, jid, out, size);
}

/* The chat as a notice names it: with the account, when agents may use more than one. */
static void chat_place(ControlServer *s, const char *jid, char *out, size_t size) {
    char name[128], label[ACCOUNT_LABEL_SIZE];
    chat_name(s, jid, name, sizeof(name));
    control_account_label(s, label, sizeof(label));
    if (label[0] && name[0]) snprintf(out, size, "%s (%s)", name, label);
    else str_copy(out, size, name);
}

/* Shows `p` in tawk's own window, as it stands now. */
static void prompt(ControlServer *s, ControlPending *p) {
    ApprovalRisk risk = automation_manager_risk(s->deps.automation, p->op, p->kind);
    ApprovalRequest req;
    memset(&req, 0, sizeof(req));
    req.id = p->approval_id;
    req.origin = p->origin;
    str_copy(req.client, sizeof(req.client), p->client);
    str_copy(req.op, sizeof(req.op), p->op);
    str_copy(req.chat_jid, sizeof(req.chat_jid), p->chat_jid);
    chat_name(s, p->chat_jid, req.chat_name, sizeof(req.chat_name));
    req.account = p->account;
    control_account_label(s, req.account_label, sizeof(req.account_label));
    str_copy(req.action, sizeof(req.action), p->action);
    req.text = p->text;
    req.editable = p->editable;
    req.danger = p->kind == WRITE_KIND_DESTRUCTIVE;
    req.risk = risk;
    req.asked_ms = p->asked_ms;
    req.expires_ms = p->expires_ms;
    s->deps.approvals->ask(s->deps.approvals, &req);
}

/* Moves `p` to the list waiting for your answer, and asks. */
static void ask(ControlServer *s, ControlPending *p) {
    if (s->pending_count >= CONTROL_MAX_PENDING || !s->deps.approvals) {
        record(s, p, AUTOMATION_OUTCOME_FAILED);
        control_fail(s, p->conn, p->request_id, "failed", "Too many requests are already waiting for an answer");
        control_pending_dispose(p);
        return;
    }
    ApprovalRisk risk = automation_manager_risk(s->deps.automation, p->op, p->kind);
    p->approval_id = ++s->next_approval;
    p->asked_ms = clock_now_ms();
    p->expires_ms = p->asked_ms + automation_manager_answer_window_ms(s->deps.automation, risk);
    p->carded = 0;
    p->card_id[0] = '\0';
    prompt(s, p);
    s->pending[s->pending_count++] = *p;
    s->changed = 1;
    cJSON *evt = cJSON_CreateObject();
    cJSON_AddStringToObject(evt, "id", p->request_id);
    cJSON_AddStringToObject(evt, "state", "waiting");
    control_tag_account(s, evt);
    control_reply(s, p->conn, control_codec_event("approval", evt));
}

/* Keeps a destructive request until its client confirms it. */
static void hold(ControlServer *s, ControlPending *p) {
    if (s->confirmation_count >= CONTROL_MAX_CONFIRMATIONS) {
        control_fail(s, p->conn, p->request_id, "failed", "Too many requests are already waiting for confirmation");
        control_pending_dispose(p);
        return;
    }
    ControlConfirmation *c = &s->confirmations[s->confirmation_count];
    memset(c, 0, sizeof(*c));
    if (automation_manager_new_token(s->deps.automation, c->token, sizeof(c->token)) != 0) {
        control_fail(s, p->conn, p->request_id, "failed", "No token could be made");
        control_pending_dispose(p);
        return;
    }
    c->pending = *p;
    c->expires_ms = clock_now_ms() + CONTROL_CONFIRM_MS;
    s->confirmation_count++;
    char name[128], summary[300];
    chat_place(s, p->chat_jid, name, sizeof(name));
    if (name[0]) snprintf(summary, sizeof(summary), "%s: %s", name, p->action);
    else str_copy(summary, sizeof(summary), p->action);
    if (summary[0] >= 'a' && summary[0] <= 'z') summary[0] = (char)(summary[0] - 'a' + 'A');
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "needs_confirmation", 1);
    cJSON_AddStringToObject(r, "token", c->token);
    cJSON_AddStringToObject(r, "summary", summary);
    cJSON_AddNumberToObject(r, "expires_at", (double)(time(NULL) + CONTROL_CONFIRM_MS / 1000));
    control_reply(s, p->conn, control_codec_ok(p->request_id, r));
}

void control_write(ControlServer *s, ControlSession *session, ControlPending *p) {
    p->conn = session->conn;
    p->origin = session->origin;
    p->account = s->account;                               /* the account this request was served by */
    str_copy(p->client, sizeof(p->client), session->client);
    if (session->paused) {
        record(s, p, AUTOMATION_OUTCOME_REFUSED);
        control_fail(s, p->conn, p->request_id, "not_allowed", "You paused this client in tawk's Agents tab");
        control_pending_dispose(p);
        return;
    }
    if (p->needs_connection && !control_connected(s)) {
        control_fail(s, p->conn, p->request_id, "offline", "tawk is not connected to WhatsApp right now");
        control_pending_dispose(p);
        return;
    }
    int retry = 0;
    char key[128];
    allowance_key(p, key, sizeof(key));
    AutomationVerdict v = automation_manager_check_write(s->deps.automation, session->origin, p->kind, clock_now_ms(), &retry);
    if (v == AUTOMATION_VERDICT_REFUSE) {
        record(s, p, AUTOMATION_OUTCOME_REFUSED);
        control_fail(s, p->conn, p->request_id, "not_allowed", p->kind == WRITE_KIND_SEND
                     ? "Sending is turned off in tawk (Settings > Automation > What they may do)"
                     : "This needs access \"manage\" or \"admin\" in tawk (Settings > Automation > What they may do)");
    } else if (v == AUTOMATION_VERDICT_RATE_LIMITED) {
        record(s, p, AUTOMATION_OUTCOME_RATE_LIMITED);
        cJSON *extra = cJSON_CreateObject();
        cJSON_AddNumberToObject(extra, "retry_after", retry);
        control_reply(s, p->conn, control_codec_error(p->request_id, "rate_limited", "Too many writes in the last minute", extra));
    } else if (p->kind == WRITE_KIND_DESTRUCTIVE) {
        hold(s, p);
        return;
    } else if (v == AUTOMATION_VERDICT_ASK && control_owner_reply(s, session, p)) {
        finish(s, p, AUTOMATION_OUTCOME_DONE);             /* an answer to you in the owner's chat reaches nobody else */
    } else if (v == AUTOMATION_VERDICT_ASK && (p->new_chat || !control_session_allows(session, p->op, key))) {
        ask(s, p);                                         /* a first message to someone is asked about whatever was allowed before */
        return;
    } else if (v == AUTOMATION_VERDICT_ASK) {
        carry_out(s, p, AUTOMATION_OUTCOME_ALLOWED);
    } else {
        carry_out(s, p, AUTOMATION_OUTCOME_DONE);
    }
    control_pending_dispose(p);
}

static int find_confirmation(ControlServer *s, const char *token, int conn) {
    for (int i = 0; token && i < s->confirmation_count; i++) {
        if (strcmp(s->confirmations[i].token, token) == 0 && s->confirmations[i].pending.conn == conn) return i;
    }
    return -1;
}

static void drop_confirmation(ControlServer *s, int index, int dispose) {
    if (dispose) control_pending_dispose(&s->confirmations[index].pending);
    s->confirmations[index] = s->confirmations[--s->confirmation_count];
}

void control_op_confirm(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    int at = find_confirmation(s, control_codec_string(req->args, "token"), session->conn);
    if (at < 0 || clock_now_ms() > s->confirmations[at].expires_ms) {
        if (at >= 0) drop_confirmation(s, at, 1);
        control_fail(s, session->conn, req->id, "bad_token", "That confirmation is unknown, used or expired");
        return;
    }
    ControlPending p = s->confirmations[at].pending;
    drop_confirmation(s, at, 0);
    str_copy(p.request_id, sizeof(p.request_id), req->id);        /* the answer goes to the confirm request */
    ask(s, &p);                                                   /* destructive: always asked in tawk too */
}

void control_op_cancel_confirmation(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    int at = find_confirmation(s, control_codec_string(req->args, "token"), session->conn);
    if (at >= 0) {
        record(s, &s->confirmations[at].pending, AUTOMATION_OUTCOME_DECLINED);
        drop_confirmation(s, at, 1);
    }
    control_reply(s, session->conn, control_codec_ok(req->id, NULL));
}

static int find_pending(ControlServer *s, int approval_id) {
    for (int i = 0; i < s->pending_count; i++) if (s->pending[i].approval_id == approval_id) return i;
    return -1;
}

static void remove_pending(ControlServer *s, int index) {
    control_pending_dispose(&s->pending[index]);
    s->pending[index] = s->pending[--s->pending_count];
    s->changed = 1;
}

/* Your own waiting request, by the id you sent it with. */
static int find_own(ControlServer *s, int conn, const char *request_id) {
    for (int i = 0; request_id && i < s->pending_count; i++) {
        if (s->pending[i].conn == conn && strcmp(s->pending[i].request_id, request_id) == 0) return i;
    }
    return -1;
}

static const char *why_not(SelfApprovalVerdict v) {
    switch (v) {
        case SELF_APPROVAL_OFF:             return "Answering your own requests needs access \"admin\" in tawk (Settings > Automation > What they may do)";
        case SELF_APPROVAL_NOT_THIS_KIND:   return "Only sends, scheduled messages, reactions, read marks and likes can be answered this way; this one waits for the user in tawk";
        case SELF_APPROVAL_CHAT_NOT_LISTED: return "That chat is not among the chats the user chose for self-approval in tawk, so this one waits for the user in tawk";
        default:                            return "The admin token is wrong or out of date";
    }
}

/* The request stays waiting for you whenever the answer here is no. */
void control_op_approve(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const char *id = control_required(s, session, req, "id");
    if (!id) return;
    int at = find_own(s, session->conn, id);
    if (at < 0) {
        control_fail(s, session->conn, req->id, "not_found", "No request of yours with that id is waiting for an answer");
        return;
    }
    ControlPending *p = &s->pending[at];
    if (session->paused) {
        control_fail(s, session->conn, req->id, "not_allowed", "You paused this client in tawk's Agents tab");
        return;
    }
    /* The rules that decide are those of the account the request was made of. */
    if (p->account != ACCOUNT_ID_NONE && control_serve_account(s, p->account) != 0) {
        control_fail(s, session->conn, req->id, "not_allowed", "That account is no longer open to agents");
        return;
    }
    if (p->new_chat) {
        control_fail(s, session->conn, req->id, "not_allowed",
                     "The first message to someone is the user's to approve in tawk; this one waits for the user");
        return;
    }
    int retry = 0;
    SelfApprovalVerdict v = automation_manager_self_approve(s->deps.automation, p->op, control_visible_chat(s, p->chat_jid),
                                                            control_codec_string(req->args, "admin_token"), clock_now_ms(), &retry);
    if (v == SELF_APPROVAL_RATE_LIMITED) {
        cJSON *extra = cJSON_CreateObject();
        cJSON_AddNumberToObject(extra, "retry_after", retry);
        control_reply(s, session->conn, control_codec_error(req->id, "rate_limited",
                      "This hour's self-approvals are used up; this one waits for the user in tawk", extra));
        return;
    }
    if (v != SELF_APPROVAL_ALLOW) {
        control_fail(s, session->conn, req->id, v == SELF_APPROVAL_BAD_TOKEN ? "bad_token" : "not_allowed", why_not(v));
        return;
    }
    if (s->deps.approvals) s->deps.approvals->withdraw(s->deps.approvals, p->approval_id);
    char name[128], notice[256];
    chat_place(s, p->chat_jid, name, sizeof(name));
    snprintf(notice, sizeof(notice), "\xF0\x9F\xA4\x96 %s answered its own request to %s%s%s", p->client, p->action, name[0] ? " in " : "", name);
    automation_manager_notice(s->deps.automation, notice);
    carry_out(s, p, AUTOMATION_OUTCOME_SELF_APPROVED);            /* the waiting request gets its own answer */
    remove_pending(s, at);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "approved", 1);
    cJSON_AddStringToObject(r, "id", id);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

/* Your answer to the waiting request at `at`, wherever you gave it. */
static void settle(ControlServer *s, int at, ApprovalAnswer *answer) {
    ControlPending *p = &s->pending[at];
    if (answer->approved) {
        if (answer->text && p->editable && (!p->text || strcmp(answer->text, p->text) != 0)) {
            free(p->text);
            p->text = answer->text;
            answer->text = NULL;
            p->edited = 1;
        }
        ControlSession *session = control_session_of(s, p->conn);
        char key[128];
        allowance_key(p, key, sizeof(key));
        if (answer->remember && session && p->kind != WRITE_KIND_DESTRUCTIVE) control_session_allow(session, p->op, key);
        carry_out(s, p, AUTOMATION_OUTCOME_APPROVED);
    } else {
        record(s, p, AUTOMATION_OUTCOME_DECLINED);
        control_fail(s, p->conn, p->request_id, "declined", "Declined by the user");
    }
    remove_pending(s, at);
}

ControlPending *control_writes_pending(ControlServer *s, int approval_id) {
    int at = find_pending(s, approval_id);
    return at >= 0 ? &s->pending[at] : NULL;
}

int control_writes_answer(ControlServer *s, int approval_id, int approved) {
    int at = find_pending(s, approval_id);
    if (at < 0) return -1;
    if (s->deps.approvals) s->deps.approvals->withdraw(s->deps.approvals, approval_id);   /* it leaves tawk's window too */
    ApprovalAnswer answer;
    memset(&answer, 0, sizeof(answer));
    answer.id = approval_id;
    answer.approved = approved;
    settle(s, at, &answer);
    return 0;
}

int control_writes_retext(ControlServer *s, int approval_id, const char *text) {
    int at = find_pending(s, approval_id);
    if (at < 0 || !text || !text[0] || !s->pending[at].editable || strlen(text) > CONTROL_MAX_TEXT_BYTES) return -1;
    ControlPending *p = &s->pending[at];
    char *copy = str_dup(text);
    if (!copy) return -1;
    free(p->text);
    p->text = copy;
    p->edited = 1;
    if (s->deps.approvals) {                                 /* tawk's window shows the new words */
        s->deps.approvals->withdraw(s->deps.approvals, approval_id);
        prompt(s, p);
    }
    s->changed = 1;
    return 0;
}

void control_writes_tick(ControlServer *s, int64_t now_ms) {
    ApprovalAnswer answer;
    while (s->deps.approvals && s->deps.approvals->take_answer(s->deps.approvals, &answer)) {
        int at = find_pending(s, answer.id);
        if (at >= 0) settle(s, at, &answer);
        approval_answer_dispose(&answer);
    }
    for (int i = s->pending_count - 1; i >= 0; i--) {
        ControlPending *p = &s->pending[i];
        if (now_ms < p->expires_ms) continue;
        s->deps.approvals->withdraw(s->deps.approvals, p->approval_id);
        record(s, p, AUTOMATION_OUTCOME_TIMED_OUT);
        control_fail(s, p->conn, p->request_id, "timed_out", "Nobody answered in tawk in time, so it was declined");
        remove_pending(s, i);
    }
    for (int i = s->confirmation_count - 1; i >= 0; i--) {
        if (now_ms > s->confirmations[i].expires_ms) drop_confirmation(s, i, 1);
    }
}

void control_writes_forget(ControlServer *s, int conn) {
    for (int i = s->pending_count - 1; i >= 0; i--) {
        if (s->pending[i].conn != conn) continue;
        if (s->deps.approvals) s->deps.approvals->withdraw(s->deps.approvals, s->pending[i].approval_id);
        record(s, &s->pending[i], AUTOMATION_OUTCOME_FAILED);
        remove_pending(s, i);
    }
    for (int i = s->confirmation_count - 1; i >= 0; i--) {
        if (s->confirmations[i].pending.conn == conn) drop_confirmation(s, i, 1);
    }
}
