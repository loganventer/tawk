/* Which connected agent writes TL;DR summaries, and telling it which
 * messages wait for one. The agent you chose does; with none chosen, the
 * only one connected does; with several and none chosen, tawk asks you in
 * your own "message yourself" chat on WhatsApp and reads the number you
 * answer with. */
#include "control_server_state.h"
#include "engines/agent_question.h"
#include "engines/summariser_choice.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

#define WANTED_BURST      4
#define ANSWER_WAIT_MS    90000     /* how long an agent has to hand a summary back */
#define MISSES_ALLOWED    2         /* unanswered requests in a row after which an agent is passed over for a while */
#define PASSED_OVER_MS    (5 * 60 * 1000)   /* and for how long: it may only have been busy */
#define WANTED_EVERY_MS   1500
#define QUESTION_LIFE_MS  (10 * 60 * 1000)
#define RETRY_ASK_MS      15000
#define SUMMARY_MAX_CHARS 400

static int label_chosen(ControlServer *s, const ControlSession *session) {
    const char *chosen = control_settings(s)->default_agent;
    if (!chosen[0]) return 0;
    char key[64];
    agent_question_label_key(session->label, key, sizeof(key));
    return strcmp(key, chosen) == 0;
}

static int eligible(const ControlSession *session) {
    return session->greeted && session->origin == CONTROL_ORIGIN_MCP && !session->paused && session->can_summarise &&
           clock_now_ms() >= session->passed_over_until_ms;
}

int control_default_agent_is(ControlServer *s, const ControlSession *session) { return label_chosen(s, session); }

int control_summariser_is(ControlServer *s, const ControlSession *session) {
    return eligible(session) && (session->summariser || label_chosen(s, session));
}

static int candidates(ControlServer *s, SummariserCandidate *out, int max) {
    int n = 0;
    for (int i = 0; i < s->session_count && n < max; i++) {
        const ControlSession *c = &s->sessions[i];
        if (!eligible(c)) continue;
        out[n].conn = c->conn;
        out[n].chosen = control_summariser_is(s, c);
        n++;
    }
    return n;
}

/* Remembers the choice by the agent's label, so it holds when that agent connects again. */
static void remember(ControlServer *s, const char *label) {
    Settings updated = *control_settings(s);
    agent_question_label_key(label, updated.default_agent, sizeof(updated.default_agent));
    settings_manager_apply(s->deps.settings, &updated);
}

static void choose(ControlServer *s, ControlSession *session) {
    for (int i = 0; i < s->session_count; i++) s->sessions[i].summariser = 0;
    session->summariser = 1;
    remember(s, session->label);
    s->summary_asking = 0;
    char notice[200];
    snprintf(notice, sizeof(notice), "\xF0\x9F\x93\x9D %.60s%s%.60s is your default agent now and writes TL;DR summaries", session->client,
             session->label[0] ? ", " : "", session->label);
    automation_manager_notice(s->deps.automation, notice);
    s->changed = 1;
}

void control_summariser_choose(ControlServer *s, int conn) {
    ControlSession *session = control_session_of(s, conn);
    if (!session) return;
    if (control_summariser_is(s, session)) {                /* chosen already: the choice is taken back */
        session->summariser = 0;
        remember(s, "");
        automation_manager_notice(s->deps.automation, "No default agent is chosen now");
        s->changed = 1;
        return;
    }
    if (session->origin != CONTROL_ORIGIN_MCP) {
        automation_manager_notice(s->deps.automation, "Only an agent acting for a model can be the default agent");
        return;
    }
    choose(s, session);
}

/* Says something to you in your own "message yourself" chat of the account being served: the number
 * the chat in question is on, which is the one you are reading. */
static int tell_yourself(ControlServer *s, const char *text) {
    if (!control_connected(s)) return -1;
    const char *self = messaging_manager_user_jid(s->deps.messaging);
    if (!self || !self[0]) return -1;
    OutgoingText out;
    memset(&out, 0, sizeof(out));
    out.text = text;
    uint64_t before = messaging_manager_live_last(s->deps.messaging);
    if (messaging_manager_send_text_to(s->deps.messaging, self, &out) != 0) return -1;
    control_owner_note_since(s, before, SENT_KIND_NOTE, 0);   /* in the owner's chat, tawk's own lines are not your words */
    s->summary_ask_account = s->account;
    return 0;
}

static void ask(ControlServer *s, const SummariserCandidate *c, int count, int64_t now) {
    AutomationSession listed[AUTOMATION_STATUS_SESSIONS];
    int n = 0;
    memset(listed, 0, sizeof(listed));
    for (int i = 0; i < count && n < AUTOMATION_STATUS_SESSIONS; i++) {
        const ControlSession *session = control_session_of(s, c[i].conn);
        if (!session) continue;
        str_copy(listed[n].client, sizeof(listed[n].client), session->client);
        str_copy(listed[n].label, sizeof(listed[n].label), session->label);
        str_copy(listed[n].doing, sizeof(listed[n].doing), session->doing);
        s->summary_ask_conns[n] = session->conn;
        n++;
    }
    char question[1600];
    agent_question_text(listed, n, question, sizeof(question));
    s->summary_asked_ms = now;                              /* asked or not, it is not tried again for a while */
    s->summary_ask_count = n;
    s->summary_asking = tell_yourself(s, question) == 0;
    /* Not sent (this account is not connected yet, say): tried again shortly, not after the whole wait. */
    if (!s->summary_asking) s->summary_asked_ms = now - QUESTION_LIFE_MS + RETRY_ASK_MS;
    if (s->summary_asking) {
        automation_manager_notice(s->deps.automation, "Several agents are connected: tawk asked you on WhatsApp which one writes "
                                  "TL;DR summaries (or choose your default agent in the Agents list with d)");
    }
}

/* The question is withdrawn when it is old, or when an agent it listed has gone. */
static void review_question(ControlServer *s, int64_t now) {
    if (!s->summary_asking) return;
    int gone = now - s->summary_asked_ms > QUESTION_LIFE_MS;
    for (int i = 0; i < s->summary_ask_count && !gone; i++) gone = control_session_of(s, s->summary_ask_conns[i]) == NULL;
    if (gone) s->summary_asking = 0;
}

/* Remembers that `session` was asked, so an agent that never answers can be told from one that does. */
static void await_summary(ControlServer *s, ControlSession *session, const char *message_id, int64_t now) {
    if (session->wait_count == CONTROL_SESSION_WAITS) {
        memmove(&session->waits[0], &session->waits[1], (CONTROL_SESSION_WAITS - 1) * sizeof(session->waits[0]));
        session->wait_count--;
    }
    ControlSummaryWait *wait = &session->waits[session->wait_count++];
    str_copy(wait->message_id, sizeof(wait->message_id), message_id);
    wait->account = s->account;
    wait->asked_ms = now;
}

void control_summary_answered(ControlSession *session, const char *message_id) {
    session->summaries_answered++;
    session->summaries_missed = 0;                          /* it answers: whatever it missed before is forgiven */
    session->passed_over_until_ms = 0;
    for (int i = 0; i < session->wait_count; i++) {
        if (strcmp(session->waits[i].message_id, message_id) != 0) continue;
        memmove(&session->waits[i], &session->waits[i + 1], (size_t)(session->wait_count - i - 1) * sizeof(session->waits[0]));
        session->wait_count--;
        return;
    }
}

/* A summary that was not handed back in time goes back on the list for another agent. An agent
 * that let several go and never handed one back does not hear tawk's requests (a client that
 * takes no channel events, say): it is passed over from then on, and you are told. */
static void review_waits(ControlServer *s, int64_t now) {
    for (int i = 0; i < s->session_count; i++) {
        ControlSession *session = &s->sessions[i];
        int kept = 0;
        for (int k = 0; k < session->wait_count; k++) {
            ControlSummaryWait *wait = &session->waits[k];
            if (now - wait->asked_ms < ANSWER_WAIT_MS) { session->waits[kept++] = *wait; continue; }
            session->summaries_missed++;
            AccountId back = s->account;
            if (control_serve_account(s, wait->account) == 0 && s->deps.summaries) summary_manager_requeue(s->deps.summaries, wait->message_id);
            control_serve_account(s, back);
        }
        session->wait_count = kept;
        if (session->can_summarise && session->summaries_missed >= MISSES_ALLOWED && now >= session->passed_over_until_ms) {
            /* For a while only: an agent in the middle of other work answers when it is done, and one that
             * hears nothing is simply passed over again each time it is tried. */
            session->summaries_missed = 0;
            session->passed_over_until_ms = now + PASSED_OVER_MS;
            char notice[240];
            snprintf(notice, sizeof(notice), "%.50s%s%.50s is not answering tawk's requests for summaries (busy, or it takes no channel "
                     "events), so another agent is asked for the next five minutes", session->client, session->label[0] ? ", " : "", session->label);
            automation_manager_notice(s->deps.automation, notice);
            s->changed = 1;
        }
    }
}

static void send_wanted(ControlServer *s, int conn, const Message *msg, const Chat *chat) {
    char sender[128];
    control_sender_name(s, msg, sender, sizeof(sender));
    cJSON *evt = cJSON_CreateObject();
    cJSON *c = cJSON_AddObjectToObject(evt, "chat");
    cJSON_AddStringToObject(c, "jid", chat->jid);
    cJSON_AddStringToObject(c, "name", chat->name);
    cJSON_AddItemToObject(evt, "message", control_message_json(s, CONTROL_ORIGIN_MCP, msg, sender));
    cJSON_AddNumberToObject(evt, "max_chars", SUMMARY_MAX_CHARS);
    control_tag_account(s, evt);
    control_reply(s, conn, control_codec_event("summary_wanted", evt));
}

static void hand_out(ControlServer *s, int64_t now) {
    SummaryManager *mgr = s->deps.summaries;
    if (!mgr) return;
    char id[64];
    while (s->summary_tokens > 0 && summary_manager_next_wanted(mgr, id, sizeof(id))) {
        Message msg;
        if (messaging_manager_get(s->deps.messaging, id, &msg) != 0) { summary_manager_drop_wanted(mgr); continue; }
        const Chat *chat = control_visible_chat(s, msg.chat_jid);
        /* A chat agents may not see, or one that left TL;DR mode while its message waited, is left alone. */
        if (!chat || !summary_manager_wants(mgr, &msg, chat)) {
            message_dispose(&msg);
            summary_manager_drop_wanted(mgr);
            continue;
        }
        SummariserCandidate c[AUTOMATION_STATUS_SESSIONS];
        int n = candidates(s, c, AUTOMATION_STATUS_SESSIONS), conn = -1;
        SummariserVerdict verdict = summariser_choice_pick(c, n, &conn);
        if (verdict == SUMMARISER_USE) {
            send_wanted(s, conn, &msg, chat);
            ControlSession *asked = control_session_of(s, conn);
            if (asked) await_summary(s, asked, msg.id, now);
            s->summary_tokens--;
            summary_manager_drop_wanted(mgr);
            message_dispose(&msg);
            continue;
        }
        message_dispose(&msg);
        /* Nobody to write it yet: it waits. With several agents and none chosen you are asked, once in a while. */
        if (verdict == SUMMARISER_ASK && !s->summary_asking && now - s->summary_asked_ms > QUESTION_LIFE_MS) ask(s, c, n, now);
        return;
    }
}

void control_summaries_tick(ControlServer *s, int64_t now) {
    review_question(s, now);
    review_waits(s, now);
    while (s->summary_tokens < WANTED_BURST && now - s->summary_refill_ms >= WANTED_EVERY_MS) {
        s->summary_tokens++;
        s->summary_refill_ms += WANTED_EVERY_MS;
    }
    if (s->summary_tokens >= WANTED_BURST) s->summary_refill_ms = now;
    int accounts = control_account_count(s);
    for (int a = 0; a < accounts; a++) {
        if (control_serve_account(s, control_account_at(s, a)) != 0) continue;
        hand_out(s, now);
    }
}

/* Your answer to the question: a number, typed by you in your own chat. */
static int take_answer(ControlServer *s, const Message *msg) {
    if (!s->summary_asking || s->account != s->summary_ask_account || !msg->from_me) return 0;
    if (strcmp(msg->chat_jid, messaging_manager_user_jid(s->deps.messaging)) != 0) return 0;
    int at = agent_question_answer(msg->text, s->summary_ask_count);
    if (at < 0) return 0;
    ControlSession *session = control_session_of(s, s->summary_ask_conns[at]);
    if (!session || !eligible(session)) { s->summary_asking = 0; return 1; }
    choose(s, session);
    char done[200];
    snprintf(done, sizeof(done), "tawk: %.60s%s%.60s writes TL;DR summaries from now on.", session->client,
             session->label[0] ? ", " : "", session->label);
    tell_yourself(s, done);
    return 1;
}

int control_summaries_on_message(ControlServer *s, const LiveMessageRef *ref) {
    SummaryManager *mgr = s->deps.summaries;
    if (!mgr && !s->summary_asking) return 0;
    Message msg;
    if (messaging_manager_get(s->deps.messaging, ref->id, &msg) != 0) return 0;
    int answered = take_answer(s, &msg);
    if (!answered && mgr) {                                 /* what you send is summarised too */
        const Chat *chat = control_visible_chat(s, msg.chat_jid);
        if (chat) summary_manager_want(mgr, &msg, chat);
    }
    message_dispose(&msg);
    return answered;
}
