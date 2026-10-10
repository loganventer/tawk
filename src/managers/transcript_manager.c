#include "managers/transcript_manager.h"
#include "engines/transcript_choice.h"
#include "engines/transcript_display_policy.h"
#include "engines/transcript_validator.h"
#include "engines/transcription_policy.h"
#include "engines/voice_language_list.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WAITING_MAX 32
#define ASKED_MAX   256

struct TranscriptManager {
    TranscriptManagerDeps deps;
    char                  error[160];
    int                   changed;
    char                  waiting[WAITING_MAX][64];     /* oldest first */
    int                   waiting_count;
    char                  asked[ASKED_MAX][64];         /* asked for since tawk started, so each is asked for once */
    int                   asked_next;
};

TranscriptManager *transcript_manager_create(const TranscriptManagerDeps *deps) {
    if (!deps || !deps->store || !deps->prefs || !deps->choices || !deps->settings) return NULL;
    TranscriptManager *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->deps = *deps;
    return m;
}

void transcript_manager_destroy(TranscriptManager *m) { free(m); }
const char *transcript_manager_error(TranscriptManager *m) { return m->error; }

static void prefs_of(TranscriptManager *m, const char *jid, ChatPrefs *out) {
    if (!jid || !jid[0] || m->deps.prefs->get(m->deps.prefs, jid, out) != 0) memset(out, 0, sizeof(*out));
}

TranscriptSaveResult transcript_manager_save(TranscriptManager *m, const Message *message, const Chat *chat,
                                             const char *language, const char *text, const char *model, const char *source,
                                             int replace_all) {
    m->error[0] = '\0';
    ChatPrefs prefs;
    prefs_of(m, chat ? chat->jid : "", &prefs);
    if (!transcription_policy_allows(chat, &prefs)) {
        str_copy(m->error, sizeof(m->error), "Voice notes in this chat are not transcribed");
        return TRANSCRIPT_OFF;
    }
    const char *why = transcript_validator_refusal(message, language, text);
    if (why) {
        str_copy(m->error, sizeof(m->error), why);
        return TRANSCRIPT_REFUSED;
    }
    Transcript t;
    transcript_init(&t);
    str_copy(t.message_id, sizeof(t.message_id), message->id);
    transcript_validator_language(language, t.language, sizeof(t.language));
    str_copy(t.model, sizeof(t.model), model ? model : "");
    str_strip_controls(t.model);
    str_copy(t.source, sizeof(t.source), source ? source : "");
    str_strip_controls(t.source);
    t.created_at = (int64_t)time(NULL);
    transcript_set_text(&t, text);
    if (t.text) transcript_validator_clean(t.text);
    int usable = t.text && t.text[0];
    if (usable && replace_all) m->deps.store->remove(m->deps.store, message->id);
    int rc = usable ? m->deps.store->save(m->deps.store, &t) : -1;
    transcript_dispose(&t);
    if (rc != 0) {
        str_copy(m->error, sizeof(m->error), "The transcript could not be kept");
        return TRANSCRIPT_FAILED;
    }
    m->changed = 1;
    return TRANSCRIPT_SAVED;
}

int transcript_manager_find(TranscriptManager *m, const char *message_id, Transcript **out, int *count) {
    return m->deps.store->find(m->deps.store, message_id, out, count);
}

int transcript_manager_best(TranscriptManager *m, const char *message_id, Transcript *out, int *others) {
    Transcript *all = NULL;
    int n = 0;
    if (others) *others = 0;
    if (m->deps.store->find(m->deps.store, message_id, &all, &n) != 0 || n <= 0) {
        transcript_array_free(all, n);
        return -1;
    }
    int pick = transcript_choice_pick(all, n, m->deps.settings->transcribe_languages);
    transcript_copy(out, &all[pick < 0 ? 0 : pick]);
    if (others) *others = n - 1;
    transcript_array_free(all, n);
    return 0;
}

int transcript_manager_shown(TranscriptManager *m, const char *chat_jid) {
    ChatPrefs prefs;
    prefs_of(m, chat_jid, &prefs);
    return transcript_display_policy_shows(m->deps.settings->show_transcripts, prefs.show_transcripts);
}

ChatTranscriptChoice transcript_manager_display_choice(TranscriptManager *m, const char *chat_jid) {
    ChatPrefs prefs;
    prefs_of(m, chat_jid, &prefs);
    return prefs.show_transcripts;
}

static int set(TranscriptManager *m, int rc) {
    if (rc != 0) {
        str_copy(m->error, sizeof(m->error), "That choice could not be saved.");
        return -1;
    }
    m->changed = 1;
    return 0;
}

int transcript_manager_set_display_choice(TranscriptManager *m, const char *chat_jid, ChatTranscriptChoice choice) {
    m->error[0] = '\0';
    if (!chat_jid || !chat_jid[0]) { str_copy(m->error, sizeof(m->error), "Open a chat first."); return -1; }
    if (choice != CHAT_TRANSCRIPT_FOLLOW && choice != CHAT_TRANSCRIPT_ALWAYS && choice != CHAT_TRANSCRIPT_NEVER) {
        str_copy(m->error, sizeof(m->error), "That is not a choice.");
        return -1;
    }
    return set(m, m->deps.choices->set_show(m->deps.choices, chat_jid, choice));
}

int transcript_manager_transcribing(TranscriptManager *m, const Chat *chat) {
    ChatPrefs prefs;
    prefs_of(m, chat ? chat->jid : "", &prefs);
    return transcription_policy_allows(chat, &prefs);
}

int transcript_manager_transcribe_chosen(TranscriptManager *m, const char *chat_jid) {
    ChatPrefs prefs;
    prefs_of(m, chat_jid, &prefs);
    return !prefs.transcribe_off;
}

int transcript_manager_set_transcribing(TranscriptManager *m, const char *chat_jid, int on) {
    m->error[0] = '\0';
    if (!chat_jid || !chat_jid[0]) { str_copy(m->error, sizeof(m->error), "Open a chat first."); return -1; }
    return set(m, m->deps.choices->set_transcribe_off(m->deps.choices, chat_jid, !on));
}

void transcript_manager_languages(TranscriptManager *m, const char *chat_jid, char *out, unsigned long size) {
    ChatPrefs prefs;
    prefs_of(m, chat_jid, &prefs);
    str_copy(out, size, prefs.voice_languages);
}

int transcript_manager_set_languages(TranscriptManager *m, const char *chat_jid, const char *languages) {
    m->error[0] = '\0';
    if (!chat_jid || !chat_jid[0]) { str_copy(m->error, sizeof(m->error), "Open a chat first."); return -1; }
    char clean[64];
    voice_language_list_clean(languages, clean, sizeof(clean));
    return set(m, m->deps.choices->set_languages(m->deps.choices, chat_jid, clean));
}

void transcript_manager_want(TranscriptManager *m, const Message *message, const Chat *chat) {
    if (!message || !message->id[0] || message->type != MESSAGE_TYPE_AUDIO || message->deleted || message->from_me) return;
    if (!m->deps.settings->transcribe_auto || !transcript_manager_transcribing(m, chat)) return;
    for (int i = 0; i < ASKED_MAX; i++) if (strcmp(m->asked[i], message->id) == 0) return;
    Transcript *have = NULL;
    int n = 0;
    m->deps.store->find(m->deps.store, message->id, &have, &n);
    transcript_array_free(have, n);
    if (n > 0) return;
    str_copy(m->asked[m->asked_next], sizeof(m->asked[0]), message->id);
    m->asked_next = (m->asked_next + 1) % ASKED_MAX;
    if (m->waiting_count == WAITING_MAX) {                  /* full: the one that waited longest gives up its place */
        memmove(m->waiting[0], m->waiting[1], (size_t)(WAITING_MAX - 1) * sizeof(m->waiting[0]));
        m->waiting_count--;
    }
    str_copy(m->waiting[m->waiting_count++], sizeof(m->waiting[0]), message->id);
}

int transcript_manager_next_wanted(TranscriptManager *m, char *message_id, unsigned long size) {
    if (m->waiting_count == 0) return 0;
    str_copy(message_id, size, m->waiting[0]);
    return 1;
}

void transcript_manager_drop_wanted(TranscriptManager *m) {
    if (m->waiting_count == 0) return;
    memmove(m->waiting[0], m->waiting[1], (size_t)(m->waiting_count - 1) * sizeof(m->waiting[0]));
    m->waiting_count--;
}

int transcript_manager_take_changed(TranscriptManager *m) {
    int was = m->changed;
    m->changed = 0;
    return was;
}
