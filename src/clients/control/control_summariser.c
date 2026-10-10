/* Which connected agent writes TL;DR summaries, and telling it which
 * messages wait for one. The agent you chose does; with none chosen, the
 * only one connected does; with several and none chosen, tawk asks you in
 * your own "message yourself" chat on WhatsApp and reads the number you
 * answer with. */
#include "control_server_state.h"
#include "engines/agent_question.h"
#include "engines/summariser_choice.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

#define WANTED_BURST      4
#define WANTED_EVERY_MS   1500
#define QUESTION_LIFE_MS  (10 * 60 * 1000)
#define SUMMARY_MAX_CHARS 400

static int label_chosen(ControlServer *s, const ControlSession *session) {
    const char *chosen = control_settings(s)->default_agent;
    if (!chosen[0]) return 0;
    char key[64];
    agent_question_label_key(session->label, key, sizeof(key));
    return strcmp(key, chosen) == 0;
}

static int eligible(const ControlSession *session) {
    return session->greeted && session->origin == CONTROL_ORIGIN_MCP && !session->paused && session->can_summarise;
}

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
    if (messaging_manager_send_text_to(s->deps.messaging, self, &out) != 0) return -1;
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
    automation_manager_notice(s->deps.automation, s->summary_asking
        ? "Several agents are connected: tawk asked you on WhatsApp which one writes TL;DR summaries (or choose your default agent in the Agents list with d)"
        : "Several agents are connected: choose your default agent in the Agents list with d");
}

/* The question is withdrawn when it is old, or when an agent it listed has gone. */
static void review_question(ControlServer *s, int64_t now) {
    if (!s->summary_asking) return;
    int gone = now - s->summary_asked_ms > QUESTION_LIFE_MS;
    for (int i = 0; i < s->summary_ask_count && !gone; i++) gone = control_session_of(s, s->summary_ask_conns[i]) == NULL;
    if (gone) s->summary_asking = 0;
}

static void send_wanted(ControlServer *s, int conn, const Message *msg, const Chat *chat) {
    char sender[128];
    control_sender_name(s, msg, sender, sizeof(sender));
    cJSON *evt = cJSON_CreateObject();
    cJSON *c = cJSON_AddObjectToObject(evt, "chat");
    cJSON_AddStringToObject(c, "jid", chat->jid);
    cJSON_AddStringToObject(c, "name", chat->name);
    cJSON_AddItemToObject(evt, "message", control_codec_message(msg, sender));
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

void control_summaries_on_message(ControlServer *s, const LiveMessageRef *ref) {
    SummaryManager *mgr = s->deps.summaries;
    if (!mgr && !s->summary_asking) return;
    Message msg;
    if (messaging_manager_get(s->deps.messaging, ref->id, &msg) != 0) return;
    if (!take_answer(s, &msg) && mgr && !msg.from_me) {
        const Chat *chat = control_visible_chat(s, msg.chat_jid);
        if (chat) summary_manager_want(mgr, &msg, chat);
    }
    message_dispose(&msg);
}
