/* Voice note transcripts in the terminal client: where the conversation
 * gets them, and your choices about them for a chat (the contact card's two
 * rows, Alt+T and /transcripts). */
#include "tui_app_state.h"
#include "core/voice_language.h"
#include "engines/transcript_display_policy.h"
#include "engines/voice_language_list.h"
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

static void default_languages(TuiApp *app, char *out, size_t size);

void tui_app_refresh_transcript_prefs(TuiApp *app) {
    ContactPanel *panel = &app->contact;
    TranscriptManager *mgr = app->deps.transcripts;
    if (!panel->open || !mgr) return;
    const Settings *s = settings_manager_current(app->deps.settings);
    const char *show = choice_text(app, transcript_manager_display_choice(mgr, panel->jid), s->show_transcripts);
    const char *transcribe = transcript_manager_transcribe_chosen(mgr, panel->jid) ? "\xE2\x9C\x93 on" : "off";
    char chosen[64], languages[96];
    transcript_manager_languages(mgr, panel->jid, chosen, sizeof(chosen));
    if (chosen[0]) snprintf(languages, sizeof(languages), "%s", chosen);
    else {
        char usual[64];
        default_languages(app, usual, sizeof(usual));
        snprintf(languages, sizeof(languages), "as Settings, Chats says (%s)", usual[0] ? usual : "any");
    }
    contact_panel_set_transcript_prefs(panel, show, transcribe, languages);
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

/* ---- a chat's voice note languages: a list of switches ------------------------ */

/* The languages as rows the switch list can show: it lists chats, so each
 * language stands in as one, its code where a chat has its address. */
static int language_rows(Chat *rows, int max) {
    int n = 0;
    for (int i = 0; i < voice_language_count() && n < max; i++) {
        const VoiceLanguage *l = voice_language_at(i);
        chat_init(&rows[n], l->code);
        snprintf(rows[n].name, sizeof(rows[n].name), "%s (%s)", l->name, l->code);
        rows[n].is_locked = 0;
        rows[n].is_archived = 0;
        n++;
    }
    return n;
}

/* The languages every chat's voice notes are taken to be in unless the chat names its own: the
 * "Transcription languages" setting without its "auto". */
static void default_languages(TuiApp *app, char *out, size_t size) {
    voice_language_list_clean(settings_manager_current(app->deps.settings)->transcribe_languages, out, size);
}

void tui_app_voice_languages_summary(TuiApp *app, char *out, size_t size) {
    char list[64];
    default_languages(app, list, sizeof(list));
    if (list[0]) snprintf(out, size, "%s", list);
    else snprintf(out, size, "any (worked out for each voice note)");
}

void tui_app_open_voice_languages(TuiApp *app, const char *jid) {
    TranscriptManager *mgr = app->deps.transcripts;
    int for_chat = jid && jid[0];
    if (for_chat && !mgr) return;
    str_copy(app->voice_languages_jid, sizeof(app->voice_languages_jid), for_chat ? jid : "");
    app->contact.open = 0;                                   /* the list takes its place, and brings the card back */
    settings_panel_close(&app->settings_panel);
    char list[64];
    default_languages(app, list, sizeof(list));
    char all_label[96];
    if (!for_chat) snprintf(all_label, sizeof(all_label), "Any language (worked out for each voice note)");
    else if (list[0]) snprintf(all_label, sizeof(all_label), "As Settings, Chats says (%s)", list);
    else snprintf(all_label, sizeof(all_label), "As Settings, Chats says (any language)");
    chat_toggle_dialog_open(&app->voice_languages,
                            for_chat ? "Languages spoken in this chat's voice notes" : "Languages spoken in voice notes, for every chat that names none",
                            all_label);
    if (for_chat) transcript_manager_languages(mgr, jid, list, sizeof(list));
    if (!list[0]) chat_toggle_dialog_set_all(&app->voice_languages, 1);
    char *save = NULL;
    for (char *code = strtok_r(list, ",", &save); code; code = strtok_r(NULL, ",", &save)) {
        chat_toggle_dialog_set_on(&app->voice_languages, code);
    }
    app->dirty = 1;
}

void tui_app_voice_languages_render(TuiApp *app, UiRect area) {
    static Chat rows[128];
    int n = language_rows(rows, 128);
    chat_toggle_dialog_render(&app->voice_languages, area, rows, n);
}

void tui_app_voice_languages_request(TuiApp *app, PopupResult result) {
    TranscriptManager *mgr = app->deps.transcripts;
    int for_chat = app->voice_languages_jid[0] != '\0';
    app->dirty = 1;
    if (result == POPUP_CHOSEN) {
        const char *codes[CHAT_TOGGLE_CAPACITY];
        int n = chat_toggle_dialog_chats(&app->voice_languages, codes, CHAT_TOGGLE_CAPACITY);
        char list[64] = "";
        size_t used = 0;
        /* The top switch on, or nothing switched on, names no language. */
        for (int i = 0; !chat_toggle_dialog_all(&app->voice_languages) && i < n && used + strlen(codes[i]) + 2 < sizeof(list); i++) {
            used += (size_t)snprintf(list + used, sizeof(list) - used, "%s%s", used ? "," : "", codes[i]);
        }
        if (for_chat && mgr) {
            if (transcript_manager_set_languages(mgr, app->voice_languages_jid, list) != 0) tui_app_toast(app, transcript_manager_error(mgr), 1);
            else if (list[0]) tui_app_toast(app, "Voice notes in this chat are written out in whichever of those languages is spoken", 0);
            else tui_app_toast(app, "This chat's voice note languages follow Settings, Chats again", 0);
        } else if (!for_chat) {
            Settings updated = *settings_manager_current(app->deps.settings);
            str_copy(updated.transcribe_languages, sizeof(updated.transcribe_languages), list[0] ? list : "auto");
            if (settings_manager_apply(app->deps.settings, &updated) != 0) tui_app_toast(app, "The setting could not be saved", 1);
            else if (list[0]) tui_app_toast(app, "Voice notes are written out in whichever of those languages is spoken, unless a chat names its own", 0);
            else tui_app_toast(app, "The language of each voice note is worked out from all languages", 0);
        }
    }
    if (!app->voice_languages.open && for_chat) tui_app_open_contact(app, app->voice_languages_jid);    /* back to the card it came from */
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
