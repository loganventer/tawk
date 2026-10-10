/* Voice note transcripts in the terminal client: where the conversation
 * gets them, and your choices about them for a chat (the contact card's two
 * rows, Alt+T and /transcripts). */
#include "tui_app_state.h"
#include "engines/transcript_display_policy.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The manager that keeps an account's transcripts: the account in view, or
 * the one a message belongs to in a chat merged across accounts. */
static TranscriptManager *manager_of(TuiApp *app, AccountId owner) {
    IAccountDirectory *dir = app->deps.directory;
    if (owner != ACCOUNT_ID_NONE && dir) {
        const AccountServices *sv = dir->find(dir, owner);
        if (sv && sv->transcripts) return sv->transcripts;
    }
    return app->deps.transcripts;
}

/* A voice note with no transcript goes on the list of those waiting for one, as it is looked at. */
static int find_transcript(void *ctx, const Message *message, AccountId owner, Transcript *out) {
    TuiApp *app = ctx;
    TranscriptManager *mgr = manager_of(app, owner);
    if (!mgr) return -1;
    if (transcript_manager_best(mgr, message->id, out, NULL) == 0) return 0;
    transcript_manager_want(mgr, message, messaging_manager_open_chat_info(app->deps.messaging));
    return -1;
}

void tui_app_init_transcripts(TuiApp *app) {
    app->transcript_source = (TranscriptSource){ app, find_transcript };
}

const TranscriptSource *tui_app_transcripts_for(TuiApp *app, const Chat *chat) {
    TranscriptManager *mgr = app->deps.transcripts;
    if (!mgr || !chat || !transcript_manager_shown(mgr, chat->jid)) return NULL;
    return &app->transcript_source;
}

static const char *choice_text(TuiApp *app, ChatTranscriptChoice choice, int on_now) {
    (void)app;
    if (choice == CHAT_TRANSCRIPT_ALWAYS) return "always";
    if (choice == CHAT_TRANSCRIPT_NEVER) return "never";
    return on_now ? "as the setting says (on)" : "as the setting says (off)";
}

void tui_app_refresh_transcript_prefs(TuiApp *app) {
    ContactPanel *panel = &app->contact;
    TranscriptManager *mgr = app->deps.transcripts;
    if (!panel->open || !mgr) return;
    const Settings *s = settings_manager_current(app->deps.settings);
    const char *show = choice_text(app, transcript_manager_display_choice(mgr, panel->jid), s->show_transcripts);
    const char *transcribe = transcript_manager_transcribe_chosen(mgr, panel->jid) ? "\xE2\x9C\x93 on" : "off";
    contact_panel_set_transcript_prefs(panel, show, transcribe);
    app->dirty = 1;
}

void tui_app_step_show_transcripts(TuiApp *app, const char *jid) {
    TranscriptManager *mgr = app->deps.transcripts;
    if (!mgr) return;
    if (!jid || !jid[0]) { tui_app_toast(app, "Select or open a chat first", 1); return; }
    ChatTranscriptChoice next = transcript_display_policy_next(transcript_manager_display_choice(mgr, jid));
    if (transcript_manager_set_display_choice(mgr, jid, next) != 0) {
        tui_app_toast(app, transcript_manager_error(mgr), 1);
        return;
    }
    /* The conversation is laid out again from the message at the top, so it stays where it was. */
    int n = 0;
    const Message *msgs = tui_app_messages(app, &n);
    message_view_hold(&app->message_view, msgs, n);
    if (!app->contact.open) {
        const Settings *s = settings_manager_current(app->deps.settings);
        char note[96];
        snprintf(note, sizeof(note), "\xF0\x9F\x93\x9D Transcripts in this chat: %s", choice_text(app, next, s->show_transcripts));
        tui_app_toast(app, note, 0);
    }
    tui_app_refresh_transcript_prefs(app);
    app->dirty = 1;
}

void tui_app_toggle_transcribing(TuiApp *app, const char *jid) {
    TranscriptManager *mgr = app->deps.transcripts;
    if (!mgr || !jid || !jid[0]) return;
    int on = !transcript_manager_transcribe_chosen(mgr, jid);
    if (transcript_manager_set_transcribing(mgr, jid, on) != 0) {
        tui_app_toast(app, transcript_manager_error(mgr), 1);
        return;
    }
    tui_app_toast(app, on ? "Voice notes in this chat are transcribed again"
                          : "New voice notes in this chat are not transcribed. Earlier transcripts are kept.", 0);
    tui_app_refresh_transcript_prefs(app);
}

int tui_app_has_transcript(TuiApp *app, const Message *m, AccountId owner) {
    TranscriptManager *mgr = manager_of(app, owner);
    if (!mgr || !m || m->type != MESSAGE_TYPE_AUDIO || m->deleted) return 0;
    Transcript *all = NULL;
    int n = 0;
    transcript_manager_find(mgr, m->id, &all, &n);
    transcript_array_free(all, n);
    return n > 0;
}

/* Every transcript of a voice note in the reader, each under its language. */
void tui_app_show_transcript(TuiApp *app, const Message *m, AccountId owner) {
    TranscriptManager *mgr = manager_of(app, owner);
    Transcript *all = NULL;
    int n = 0;
    if (!mgr || !m || transcript_manager_find(mgr, m->id, &all, &n) != 0 || n <= 0) {
        transcript_array_free(all, n);
        tui_app_toast(app, "This voice note has no transcript", 1);
        return;
    }
    size_t size = 1;
    for (int i = 0; i < n; i++) size += (all[i].text ? strlen(all[i].text) : 0) + sizeof(all[i].language) + 8;
    char *text = calloc(1, size);
    if (text) {
        size_t used = 0;
        for (int i = 0; i < n; i++) {
            if (n > 1 && all[i].language[0]) used += (size_t)snprintf(text + used, size - used, "[%s]\n", all[i].language);
            used += (size_t)snprintf(text + used, size - used, "%s%s", all[i].text ? all[i].text : "", i + 1 < n ? "\n\n" : "");
        }
        text_reader_open(&app->reader, " \xF0\x9F\x93\x9D Transcript ", text);
        free(text);
        app->dirty = 1;
    }
    transcript_array_free(all, n);
}
