#include "managers/summary_manager.h"
#include "engines/summary_policy.h"
#include "engines/summary_validator.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WAITING_MAX  512
#define ASKED_MAX    1024
#define FILLED_MAX   64
#define BACK_LOOK    600        /* how many of a chat's newest messages are looked through */

struct SummaryManager {
    SummaryManagerDeps deps;
    char               error[160];
    int                changed;
    char               waiting[WAITING_MAX][64];     /* oldest first */
    int                waiting_count;
    char               asked[ASKED_MAX][64];         /* asked for since tawk started, so each is asked for once */
    int                asked_next;
    char               filled[FILLED_MAX][128];      /* chats whose older messages were already looked through */
    int                filled_next;
};

SummaryManager *summary_manager_create(const SummaryManagerDeps *deps) {
    if (!deps || !deps->store || !deps->prefs || !deps->choices || !deps->settings) return NULL;
    SummaryManager *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->deps = *deps;
    return m;
}

void summary_manager_destroy(SummaryManager *m) { free(m); }
const char *summary_manager_error(SummaryManager *m) { return m->error; }

static void prefs_of(SummaryManager *m, const char *jid, ChatPrefs *out) {
    if (!jid || !jid[0] || m->deps.prefs->get(m->deps.prefs, jid, out) != 0) memset(out, 0, sizeof(*out));
}

int summary_manager_tldr(SummaryManager *m, const char *chat_jid) {
    ChatPrefs prefs;
    prefs_of(m, chat_jid, &prefs);
    return prefs.tldr;
}

int summary_manager_set_tldr(SummaryManager *m, const char *chat_jid, int on) {
    m->error[0] = '\0';
    if (!chat_jid || !chat_jid[0]) { str_copy(m->error, sizeof(m->error), "Open a chat first."); return -1; }
    if (m->deps.choices->set_tldr(m->deps.choices, chat_jid, on) != 0) {
        str_copy(m->error, sizeof(m->error), "That choice could not be saved.");
        return -1;
    }
    for (int i = 0; i < FILLED_MAX; i++) if (strcmp(m->filled[i], chat_jid) == 0) m->filled[i][0] = '\0';   /* looked through again when next shown */
    m->changed = 1;
    return 0;
}

SummarySaveResult summary_manager_save(SummaryManager *m, const Message *message, const Chat *chat,
                                       const char *text, const char *model, const char *source) {
    m->error[0] = '\0';
    ChatPrefs prefs;
    prefs_of(m, chat ? chat->jid : "", &prefs);
    if (!summary_policy_allows(chat, &prefs)) {
        str_copy(m->error, sizeof(m->error), "This chat is not in TL;DR mode");
        return SUMMARY_OFF;
    }
    const char *why = summary_validator_refusal(message, text);
    if (why) {
        str_copy(m->error, sizeof(m->error), why);
        return SUMMARY_REFUSED;
    }
    Summary s;
    summary_init(&s);
    str_copy(s.message_id, sizeof(s.message_id), message->id);
    str_copy(s.model, sizeof(s.model), model ? model : "");
    str_strip_controls(s.model);
    str_copy(s.source, sizeof(s.source), source ? source : "");
    str_strip_controls(s.source);
    s.created_at = (int64_t)time(NULL);
    summary_set_text(&s, text);
    if (s.text) summary_validator_clean(s.text);
    int rc = s.text && s.text[0] ? m->deps.store->save(m->deps.store, &s) : -1;
    summary_dispose(&s);
    if (rc != 0) {
        str_copy(m->error, sizeof(m->error), "The summary could not be kept");
        return SUMMARY_FAILED;
    }
    m->changed = 1;
    return SUMMARY_SAVED;
}

int summary_manager_find(SummaryManager *m, const char *message_id, Summary *out) {
    return m->deps.store->find(m->deps.store, message_id, out);
}

int summary_manager_wants(SummaryManager *m, const Message *message, const Chat *chat) {
    ChatPrefs prefs;
    prefs_of(m, chat ? chat->jid : "", &prefs);
    return summary_policy_wants(chat, &prefs, message, m->deps.settings->tldr_min_chars);
}

static int listed(char list[][64], int count, const char *id) {
    for (int i = 0; i < count; i++) if (strcmp(list[i], id) == 0) return 1;
    return 0;
}

void summary_manager_want(SummaryManager *m, const Message *message, const Chat *chat) {
    if (!message || !message->id[0] || listed(m->asked, ASKED_MAX, message->id)) return;
    if (!summary_manager_wants(m, message, chat)) return;
    Summary have;
    if (m->deps.store->find(m->deps.store, message->id, &have) == 0) { summary_dispose(&have); return; }
    str_copy(m->asked[m->asked_next], sizeof(m->asked[0]), message->id);
    m->asked_next = (m->asked_next + 1) % ASKED_MAX;
    if (m->waiting_count == WAITING_MAX) {                  /* full: the one that waited longest gives up its place */
        memmove(m->waiting[0], m->waiting[1], (size_t)(WAITING_MAX - 1) * sizeof(m->waiting[0]));
        m->waiting_count--;
    }
    str_copy(m->waiting[m->waiting_count++], sizeof(m->waiting[0]), message->id);
}

int summary_manager_backfill(SummaryManager *m, const Chat *chat, int64_t now) {
    int days = m->deps.settings->tldr_back_days;
    if (!m->deps.messages || !chat || !chat->jid[0] || days <= 0) return 0;
    for (int i = 0; i < FILLED_MAX; i++) if (strcmp(m->filled[i], chat->jid) == 0) return 0;
    ChatPrefs prefs;
    prefs_of(m, chat->jid, &prefs);
    if (!summary_policy_allows(chat, &prefs)) return 0;
    str_copy(m->filled[m->filled_next], sizeof(m->filled[0]), chat->jid);
    m->filled_next = (m->filled_next + 1) % FILLED_MAX;
    Message *recent = NULL;
    int count = 0, added = 0;
    if (m->deps.messages->recent(m->deps.messages, chat->jid, BACK_LOOK, &recent, &count) != 0) return 0;
    int64_t since = now - (int64_t)days * 86400;
    for (int i = count - 1; i >= 0; i--) {                  /* newest first: what you read first is summarised first */
        if (recent[i].timestamp < since) break;
        if (recent[i].from_me) continue;
        int before = m->waiting_count;
        summary_manager_want(m, &recent[i], chat);
        if (m->waiting_count > before) added++;
    }
    message_array_free(recent, count);
    return added;
}

void summary_manager_requeue(SummaryManager *m, const char *message_id) {
    if (!message_id || !message_id[0] || listed(m->waiting, m->waiting_count, message_id)) return;
    if (m->waiting_count == WAITING_MAX) m->waiting_count--;                 /* the newest gives up its place */
    memmove(m->waiting[1], m->waiting[0], (size_t)m->waiting_count * sizeof(m->waiting[0]));
    str_copy(m->waiting[0], sizeof(m->waiting[0]), message_id);
    m->waiting_count++;
}

int summary_manager_next_wanted(SummaryManager *m, char *message_id, size_t size) {
    if (m->waiting_count == 0) return 0;
    str_copy(message_id, size, m->waiting[0]);
    return 1;
}

void summary_manager_drop_wanted(SummaryManager *m) {
    if (m->waiting_count == 0) return;
    memmove(m->waiting[0], m->waiting[1], (size_t)(m->waiting_count - 1) * sizeof(m->waiting[0]));
    m->waiting_count--;
}

int summary_manager_take_changed(SummaryManager *m) {
    int was = m->changed;
    m->changed = 0;
    return was;
}
