/* Voice note transcripts: what may be kept, which one shows, the store
 * (one per message and language, each account its own, gone with their
 * message), your choices for a chat, and that switching a chat off looks
 * forwards only. */
#include "clients/tui/message_view.h"
#include "clients/tui/transcript_view.h"
#include "clients/tui/tui_palette.h"
#include "contracts/i_message_store.h"
#include "engines/transcript_choice.h"
#include "engines/transcript_display_policy.h"
#include "engines/transcript_validator.h"
#include "core/voice_language.h"
#include "engines/transcription_policy.h"
#include "engines/voice_language_list.h"
#include "managers/transcript_manager.h"
#include "resource_access/sqlite_chat_prefs_store.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_transcript_store.h"
#include "utilities/str_util.h"

#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define MOM  "27820000001@s.whatsapp.net"
#define WORK "120363000000000001@g.us"

static void voice_note(Message *m, const char *id, const char *jid) {
    message_init(m);
    str_copy(m->id, sizeof(m->id), id);
    str_copy(m->chat_jid, sizeof(m->chat_jid), jid);
    str_copy(m->sender_jid, sizeof(m->sender_jid), jid);
    m->type = MESSAGE_TYPE_AUDIO;
    m->timestamp = 1790000000;
    m->duration_s = 14;
}

static void test_engines(void) {
    Message audio, text;
    voice_note(&audio, "A1", MOM);
    voice_note(&text, "T1", MOM);
    text.type = MESSAGE_TYPE_TEXT;
    CHECK(transcript_validator_refusal(&audio, "af", "Hallo daar") == NULL, "a voice note takes a transcript");
    CHECK(transcript_validator_refusal(&audio, NULL, "Hallo") == NULL && transcript_validator_refusal(&audio, "", "Hallo") == NULL,
          "with or without a language");
    CHECK(transcript_validator_refusal(&text, "af", "Hallo") != NULL, "a text message does not");
    CHECK(transcript_validator_refusal(&audio, "af", "") != NULL && transcript_validator_refusal(&audio, "af", " \n ") != NULL &&
          transcript_validator_refusal(&audio, "af", NULL) != NULL, "nor do no words at all");
    CHECK(transcript_validator_refusal(&audio, "af; DROP", "Hallo") != NULL &&
          transcript_validator_refusal(&audio, "a-very-long-language-code", "Hallo") != NULL, "a language is a short code");
    char *big = malloc(TRANSCRIPT_MAX_BYTES + 2);
    memset(big, 'a', TRANSCRIPT_MAX_BYTES + 1);
    big[TRANSCRIPT_MAX_BYTES + 1] = '\0';
    CHECK(transcript_validator_refusal(&audio, "af", big) != NULL, "a transcript has a size limit");
    free(big);
    audio.deleted = 1;
    CHECK(transcript_validator_refusal(&audio, "af", "Hallo") != NULL, "a deleted voice note takes none");

    char dirty[] = "  een\ttwee\x1b[31m\r\ndrie\x07  ";
    transcript_validator_clean(dirty);
    CHECK(strcmp(dirty, "een twee [31m \ndrie") == 0, "control characters become spaces, line breaks stay, the ends are trimmed");
    char lang[16];
    transcript_validator_language("AF", lang, sizeof(lang));
    CHECK(strcmp(lang, "af") == 0, "a language is stored in lower case");
    transcript_validator_language("auto", lang, sizeof(lang));
    CHECK(lang[0] == '\0', "auto is no language");

    Transcript two[2];
    transcript_init(&two[0]);
    transcript_init(&two[1]);
    str_copy(two[0].language, sizeof(two[0].language), "en");
    str_copy(two[1].language, sizeof(two[1].language), "af");
    CHECK(transcript_choice_pick(two, 2, "af,en") == 1 && transcript_choice_pick(two, 2, "en, af") == 0, "the first of your languages wins");
    CHECK(transcript_choice_pick(two, 2, "auto") == 0 && transcript_choice_pick(two, 2, "zu") == 0 && transcript_choice_pick(two, 2, NULL) == 0,
          "else the newest");
    CHECK(transcript_choice_pick(two, 0, "af") == -1, "and none when there is none");

    CHECK(transcript_display_policy_shows(1, CHAT_TRANSCRIPT_FOLLOW) && !transcript_display_policy_shows(0, CHAT_TRANSCRIPT_FOLLOW),
          "a chat with nothing chosen follows the setting");
    CHECK(transcript_display_policy_shows(0, CHAT_TRANSCRIPT_ALWAYS) && transcript_display_policy_shows(1, CHAT_TRANSCRIPT_ALWAYS),
          "always shows whatever the setting says");
    CHECK(!transcript_display_policy_shows(1, CHAT_TRANSCRIPT_NEVER) && !transcript_display_policy_shows(0, CHAT_TRANSCRIPT_NEVER),
          "never hides whatever the setting says");
    CHECK(transcript_display_policy_next(CHAT_TRANSCRIPT_FOLLOW) == CHAT_TRANSCRIPT_ALWAYS &&
          transcript_display_policy_next(CHAT_TRANSCRIPT_ALWAYS) == CHAT_TRANSCRIPT_NEVER &&
          transcript_display_policy_next(CHAT_TRANSCRIPT_NEVER) == CHAT_TRANSCRIPT_FOLLOW, "stepping goes round the three");

    Chat chat;
    chat_init(&chat, MOM);
    chat.is_locked = 0;
    ChatPrefs prefs;
    memset(&prefs, 0, sizeof(prefs));
    CHECK(transcription_policy_allows(&chat, &prefs) && transcription_policy_allows(&chat, NULL), "a chat is transcribed unless you say otherwise");
    prefs.transcribe_off = 1;
    CHECK(!transcription_policy_allows(&chat, &prefs), "not once you switched it off");
    prefs.transcribe_off = 0;
    chat.soft_locked = 1;
    CHECK(!transcription_policy_allows(&chat, &prefs), "never a soft-locked chat");
    chat.soft_locked = 0;
    chat.is_locked = 1;
    CHECK(!transcription_policy_allows(&chat, &prefs) && !transcription_policy_allows(NULL, &prefs), "never a locked one, or none");
    char list[64];
    CHECK(voice_language_list_clean(" AF, en,af , xx,,klingon", list, sizeof(list)) == 2 && strcmp(list, "af,en") == 0,
          "a list of languages is kept clean: known codes, lower case, each once, in the order given");
    CHECK(voice_language_list_clean("", list, sizeof(list)) == 0 && list[0] == '\0' && voice_language_list_clean(NULL, list, sizeof(list)) == 0,
          "and an empty one stays empty");
    CHECK(voice_language_find("af") && strcmp(voice_language_find("AF")->name, "Afrikaans") == 0 && !voice_language_find("xx") &&
          voice_language_count() > 90, "the languages a transcriber can tell apart are known by name");
    message_dispose(&audio);
    message_dispose(&text);
}

static int count_for(ITranscriptStore *store, const char *id) {
    Transcript *all = NULL;
    int n = 0;
    store->find(store, id, &all, &n);
    transcript_array_free(all, n);
    return n;
}

static void put(ITranscriptStore *store, const char *id, const char *language, const char *text, int64_t at) {
    Transcript t;
    transcript_init(&t);
    str_copy(t.message_id, sizeof(t.message_id), id);
    str_copy(t.language, sizeof(t.language), language);
    str_copy(t.model, sizeof(t.model), "large-v3");
    str_copy(t.source, sizeof(t.source), "test");
    t.created_at = at;
    transcript_set_text(&t, text);
    CHECK(store->save(store, &t) == 0, "a transcript is saved");
    transcript_dispose(&t);
}

static void test_store(sqlite3 *db) {
    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    ITranscriptStore *first = sqlite_transcript_store_create(db, ACCOUNT_ID_FIRST);
    ITranscriptStore *second = sqlite_transcript_store_create(db, ACCOUNT_ID_FIRST + 1);
    Message m;
    voice_note(&m, "V1", MOM);
    messages->save(messages, &m);
    message_dispose(&m);
    voice_note(&m, "V2", MOM);
    messages->save(messages, &m);
    message_dispose(&m);

    CHECK(count_for(first, "V1") == 0, "a voice note starts with no transcript");
    put(first, "V1", "af", "Hallo my kind", 100);
    put(first, "V1", "en", "Hello my child", 200);
    put(first, "V2", "", "Bring bread", 300);
    Transcript *all = NULL;
    int n = 0;
    CHECK(first->find(first, "V1", &all, &n) == 0 && n == 2 && strcmp(all[0].language, "en") == 0 &&
          strcmp(all[1].text, "Hallo my kind") == 0 && strcmp(all[1].model, "large-v3") == 0, "one per language, newest first");
    transcript_array_free(all, n);
    put(first, "V1", "af", "Hallo my kind, hoe gaan dit", 400);
    CHECK(first->find(first, "V1", &all, &n) == 0 && n == 2 && strcmp(all[0].language, "af") == 0 &&
          strcmp(all[0].text, "Hallo my kind, hoe gaan dit") == 0, "a second one in the same language replaces the first");
    transcript_array_free(all, n);
    CHECK(count_for(second, "V1") == 0, "another account sees none of them");
    put(second, "V1", "af", "Iets anders", 500);
    CHECK(count_for(second, "V1") == 1 && count_for(first, "V1") == 2, "and keeps its own under the same message id");

    CHECK(messages->remove(messages, "V2") == 0 && count_for(first, "V2") == 0, "a message deleted here takes its transcript with it");
    CHECK(messages->edit_text(messages, "V1", "edited", 0) == 0 && count_for(first, "V1") == 2, "an edit leaves it alone");
    CHECK(messages->edit_text(messages, "V1", NULL, 1) == 0 && count_for(first, "V1") == 0, "and so does one deleted for everyone");
    CHECK(count_for(second, "V1") == 1, "in its own account only");
    CHECK(second->remove(second, "V1") == 0 && count_for(second, "V1") == 0, "one can be removed by hand");
    first->destroy(first);
    second->destroy(second);
    messages->destroy(messages);
}

static void test_manager(sqlite3 *db) {
    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    ITranscriptStore *store = sqlite_transcript_store_create(db, ACCOUNT_ID_FIRST);
    IChatPrefsStore *prefs = sqlite_chat_prefs_store_create(db);
    Settings settings;
    settings_set_defaults(&settings);
    str_copy(settings.transcribe_languages, sizeof(settings.transcribe_languages), "af,en");
    TranscriptManagerDeps deps = { store, prefs, sqlite_chat_prefs_store_transcripts(prefs), &settings };
    TranscriptManager *mgr = transcript_manager_create(&deps);
    TranscriptManagerDeps missing = { store, NULL, NULL, &settings };
    CHECK(mgr != NULL && transcript_manager_create(&missing) == NULL, "the manager needs every part");

    Chat mom, work;
    chat_init(&mom, MOM);
    chat_init(&work, WORK);
    mom.is_locked = work.is_locked = 0;
    Message a, b, c, text;
    voice_note(&a, "M1", MOM);
    voice_note(&b, "M2", MOM);
    voice_note(&c, "W1", WORK);
    voice_note(&text, "M3", MOM);
    text.type = MESSAGE_TYPE_TEXT;
    messages->save(messages, &a);
    messages->save(messages, &b);
    messages->save(messages, &c);

    CHECK(transcript_manager_save(mgr, &a, &mom, "EN", "  Hello\tthere \x1b ", "large-v3", "tawk-mcp", 0) == TRANSCRIPT_SAVED, "a transcript is kept");
    CHECK(transcript_manager_take_changed(mgr) && !transcript_manager_take_changed(mgr), "the change is reported once");
    CHECK(transcript_manager_save(mgr, &a, &mom, "af", "Hallo daar", "large-v3", "tawk-mcp", 0) == TRANSCRIPT_SAVED, "and one in another language");
    Transcript best;
    transcript_init(&best);
    int others = -1;
    CHECK(transcript_manager_best(mgr, "M1", &best, &others) == 0 && strcmp(best.language, "af") == 0 && others == 1,
          "the one shown is in the first of your languages");
    transcript_dispose(&best);
    str_copy(settings.transcribe_languages, sizeof(settings.transcribe_languages), "en");
    transcript_init(&best);
    CHECK(transcript_manager_best(mgr, "M1", &best, NULL) == 0 && strcmp(best.text, "Hello there") == 0 && strcmp(best.source, "tawk-mcp") == 0,
          "and it was cleaned on the way in");
    transcript_dispose(&best);
    transcript_init(&best);
    CHECK(transcript_manager_best(mgr, "M2", &best, NULL) != 0, "a voice note without one has none to show");
    transcript_dispose(&best);
    CHECK(transcript_manager_save(mgr, &text, &mom, "af", "Hallo", "", "", 0) == TRANSCRIPT_REFUSED && transcript_manager_error(mgr)[0],
          "a text message is refused, with a reason");

    CHECK(transcript_manager_save(mgr, &a, &mom, "af", "Hallo daar, hoe gaan dit", "large-v3", "tawk-mcp", 1) == TRANSCRIPT_SAVED, "a voice note is written out again");
    Transcript *again = NULL;
    int again_count = 0;
    transcript_manager_find(mgr, "M1", &again, &again_count);
    CHECK(again_count == 1 && strcmp(again[0].text, "Hallo daar, hoe gaan dit") == 0, "which takes the place of every transcript it had");
    transcript_array_free(again, again_count);
    transcript_manager_save(mgr, &a, &mom, "en", "Hello there", "large-v3", "tawk-mcp", 0);

    /* showing: the setting and the chat's own choice */
    CHECK(transcript_manager_shown(mgr, MOM) && transcript_manager_display_choice(mgr, MOM) == CHAT_TRANSCRIPT_FOLLOW, "shown while the setting is on");
    settings.show_transcripts = 0;
    CHECK(!transcript_manager_shown(mgr, MOM), "hidden when it is off");
    CHECK(transcript_manager_set_display_choice(mgr, MOM, CHAT_TRANSCRIPT_ALWAYS) == 0 && transcript_manager_shown(mgr, MOM) &&
          !transcript_manager_shown(mgr, WORK), "one chat can always show them");
    settings.show_transcripts = 1;
    CHECK(transcript_manager_set_display_choice(mgr, MOM, CHAT_TRANSCRIPT_NEVER) == 0 && !transcript_manager_shown(mgr, MOM) &&
          transcript_manager_shown(mgr, WORK), "or never, with the setting on");
    CHECK(transcript_manager_set_display_choice(mgr, MOM, (ChatTranscriptChoice)7) != 0 && transcript_manager_set_display_choice(mgr, "", CHAT_TRANSCRIPT_ALWAYS) != 0,
          "anything else is refused");
    transcript_init(&best);
    CHECK(transcript_manager_best(mgr, "M1", &best, NULL) == 0, "hiding removes nothing");
    transcript_dispose(&best);

    /* transcribing: off for one chat, forwards only */
    CHECK(transcript_manager_transcribing(mgr, &mom) && transcript_manager_transcribe_chosen(mgr, MOM), "a chat is transcribed to begin with");
    CHECK(transcript_manager_set_transcribing(mgr, MOM, 0) == 0 && !transcript_manager_transcribing(mgr, &mom) &&
          !transcript_manager_transcribe_chosen(mgr, MOM) && transcript_manager_transcribing(mgr, &work), "one chat can be switched off");
    CHECK(transcript_manager_save(mgr, &b, &mom, "af", "Nuwe stemnota", "", "tawk-mcp", 0) == TRANSCRIPT_OFF, "a new transcript for it is refused");
    transcript_init(&best);
    CHECK(transcript_manager_best(mgr, "M2", &best, NULL) != 0, "and nothing is kept");
    transcript_dispose(&best);
    transcript_init(&best);
    CHECK(transcript_manager_best(mgr, "M1", &best, NULL) == 0 && strcmp(best.text, "Hello there") == 0,
          "the transcripts it already had are still there");
    transcript_dispose(&best);
    CHECK(transcript_manager_save(mgr, &c, &work, "", "Stand-up in five", "", "tawk-mcp", 0) == TRANSCRIPT_SAVED, "another chat carries on");
    CHECK(transcript_manager_set_transcribing(mgr, MOM, 1) == 0 &&
          transcript_manager_save(mgr, &b, &mom, "af", "Nuwe stemnota", "", "tawk-mcp", 0) == TRANSCRIPT_SAVED, "and it can be switched on again");
    mom.soft_locked = 1;
    CHECK(transcript_manager_save(mgr, &b, &mom, "en", "New voice note", "", "tawk-mcp", 0) == TRANSCRIPT_OFF && transcript_manager_transcribe_chosen(mgr, MOM),
          "a soft-locked chat takes none, whatever was chosen");
    mom.soft_locked = 0;

    /* older voice notes that are looked at wait for a transcript, once each */
    Message old, mine;
    voice_note(&old, "OLD1", WORK);
    voice_note(&mine, "MINE1", WORK);
    mine.from_me = 1;
    messages->save(messages, &old);
    char wanted[64];
    settings.transcribe_auto = 0;
    transcript_manager_want(mgr, &old, &work);
    CHECK(!transcript_manager_next_wanted(mgr, wanted, sizeof(wanted)), "with automatic transcription off, looking at a voice note asks for nothing");
    settings.transcribe_auto = 1;
    transcript_manager_want(mgr, &old, &work);
    transcript_manager_want(mgr, &old, &work);
    transcript_manager_want(mgr, &mine, &work);
    transcript_manager_want(mgr, &c, &work);
    transcript_manager_want(mgr, &text, &mom);
    CHECK(transcript_manager_next_wanted(mgr, wanted, sizeof(wanted)) && strcmp(wanted, "OLD1") == 0, "with it on, one without a transcript waits for one");
    transcript_manager_drop_wanted(mgr);
    CHECK(!transcript_manager_next_wanted(mgr, wanted, sizeof(wanted)), "once, and never your own, one that has a transcript, or a text message");
    transcript_manager_set_transcribing(mgr, WORK, 0);
    voice_note(&old, "OLD2", WORK);
    transcript_manager_want(mgr, &old, &work);
    CHECK(!transcript_manager_next_wanted(mgr, wanted, sizeof(wanted)), "nor any in a chat that is switched off");
    transcript_manager_set_transcribing(mgr, WORK, 1);
    message_dispose(&old);
    message_dispose(&mine);

    /* the languages a chat's voice notes are spoken in */
    char spoken[64];
    transcript_manager_languages(mgr, MOM, spoken, sizeof(spoken));
    CHECK(spoken[0] == '\0', "a chat names no languages to begin with: the transcriber works it out");
    CHECK(transcript_manager_set_languages(mgr, MOM, "en, AF,zz") == 0, "languages can be switched on for a chat");
    transcript_manager_languages(mgr, MOM, spoken, sizeof(spoken));
    CHECK(strcmp(spoken, "en,af") == 0, "kept clean, in the order chosen");
    transcript_manager_languages(mgr, WORK, spoken, sizeof(spoken));
    CHECK(spoken[0] == '\0', "for that chat alone");
    CHECK(transcript_manager_set_languages(mgr, MOM, "") == 0, "and all switched off again");
    transcript_manager_languages(mgr, MOM, spoken, sizeof(spoken));
    CHECK(spoken[0] == '\0', "which leaves it to the transcriber");
    transcript_manager_set_languages(mgr, "1234@lid", "af");

    /* what you chose survives the chat turning out to have another address */
    ChatPrefs got;
    transcript_manager_set_display_choice(mgr, "1234@lid", CHAT_TRANSCRIPT_ALWAYS);
    transcript_manager_set_transcribing(mgr, "1234@lid", 0);
    CHECK(prefs->reassign_jid(prefs, "1234@lid", "27820000005@s.whatsapp.net") == 0 &&
          prefs->get(prefs, "27820000005@s.whatsapp.net", &got) == 0 && got.show_transcripts == CHAT_TRANSCRIPT_ALWAYS && got.transcribe_off == 1 && strcmp(got.voice_languages, "af") == 0,
          "the choices move with the chat");

    /* the conversation's view of one */
    Transcript shown;
    transcript_init(&shown);
    str_copy(shown.language, sizeof(shown.language), "af");
    transcript_set_text(&shown, "een twee drie vier vyf ses sewe agt nege tien elf twaalf");
    TranscriptView view;
    memset(&view, 0, sizeof(view));
    view.found = 1;
    view.transcript = shown;
    TextLine *lines = NULL;
    int kept = transcript_view_wrap(&view, 10, &lines);
    CHECK(kept >= 6 && lines[0].offset == 0 && lines[kept - 1].offset + lines[kept - 1].length == strlen(shown.text),
          "a long transcript is wrapped whole, down to its last word");
    free(lines);
    kept = transcript_view_wrap(&view, 80, &lines);
    CHECK(kept == 1, "a short one is one line");
    free(lines);
    view.found = 0;
    kept = transcript_view_wrap(&view, 80, &lines);
    CHECK(kept == 0 && lines == NULL, "and none gives no lines");
    transcript_dispose(&shown);

    message_dispose(&a);
    message_dispose(&b);
    message_dispose(&c);
    message_dispose(&text);
    transcript_manager_destroy(mgr);
    prefs->destroy(prefs);
    store->destroy(store);
    messages->destroy(messages);
}

/* ---- the conversation, drawn on a screen nobody sees ------------------------- */

static int fake_find(void *ctx, const Message *message, AccountId owner, Transcript *out) {
    (void)owner;
    if (strcmp(message->id, "V1") != 0) return -1;
    str_copy(out->language, sizeof(out->language), "af");
    transcript_set_text(out, (const char *)ctx);
    return 0;
}

/* Whether `words` is drawn anywhere on the screen. */
static int on_screen(const char *words) {
    char line[512];
    for (int y = 0; y < LINES; y++) {
        if (mvinnstr(y, 0, line, sizeof(line) - 1) != ERR && strstr(line, words)) return 1;
    }
    return 0;
}

static void test_conversation(void) {
    setlocale(LC_ALL, "");
    FILE *out = fopen("/dev/null", "w"), *in = fopen("/dev/null", "r");
    SCREEN *screen = out && in ? newterm("xterm-256color", out, in) : NULL;
    CHECK(screen != NULL, "a screen to draw on");
    if (!screen) return;
    tui_palette_init();
    resizeterm(40, 100);

    Message msgs[2];
    voice_note(&msgs[0], "V1", MOM);
    voice_note(&msgs[1], "V2", MOM);
    char words[] = "alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima mike november oscar papa "
                   "quebec romeo sierra tango uniform victor whiskey xray yankee zulu alpha bravo charlie delta echo "
                   "foxtrot golf hotel india juliet kilo lima mike november oscar papa quebec romeo lastword";
    TranscriptSource source = { words, fake_find };
    MessageViewContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.title = "Mom";
    ctx.playing_path = "";
    UiRect rect = { 0, 0, 40, 100 };
    MessageView view;
    message_view_init(&view);

    erase();
    ctx.transcripts = &source;
    message_view_render(&view, rect, msgs, 2, &ctx);
    int rows_with = view.row_count;
    CHECK(on_screen("alpha bravo"), "a voice note's transcript is drawn");
    /* It is part of the voice note's own bubble: its rows sit between the play line and the bubble's lower edge. */
    int media = -1, first_spoken = -1, last_spoken = -1, bottom = -1, top = -1;
    for (int i = 0; i < view.row_count; i++) {
        if (view.rows[i].message != 0) continue;
        if (view.rows[i].kind == MESSAGE_ROW_EDGE_TOP) top = i;
        if (view.rows[i].kind == MESSAGE_ROW_MEDIA) media = i;
        if (view.rows[i].kind == MESSAGE_ROW_TRANSCRIPT) { if (first_spoken < 0) first_spoken = i; last_spoken = i; }
        if (view.rows[i].kind == MESSAGE_ROW_EDGE_BOTTOM) bottom = i;
    }
    CHECK(top >= 0 && media == top + 1 && first_spoken == media + 1 && bottom == last_spoken + 1,
          "inside the voice note's bubble, straight under the play line");
    CHECK(view.rows[first_spoken].x == view.rows[media].x && view.rows[first_spoken].width == view.rows[media].width,
          "as wide as the rest of the bubble");
    int found_dim = 0;
    for (int y = 0; y < LINES && !found_dim; y++) {
        char line[512];
        if (mvinnstr(y, 0, line, sizeof(line) - 1) == ERR) continue;
        const char *at = strstr(line, "alpha bravo");
        if (at) found_dim = (mvinch(y, (int)(at - line)) & A_DIM) != 0;
    }
    CHECK(found_dim, "in grey, apart from the voice note's own line");
    CHECK(on_screen("lastword"), "all of it, down to the last word: a transcript is never cut");

    erase();
    ctx.transcripts = NULL;
    message_view_render(&view, rect, msgs, 2, &ctx);
    CHECK(!on_screen("alpha bravo") && view.row_count < rows_with, "switched off, nothing of it is drawn");

    erase();
    ctx.transcripts = &source;
    ctx.veiled = 1;
    message_view_render(&view, rect, msgs, 2, &ctx);
    CHECK(!on_screen("alpha bravo"), "a soft-locked chat shows its shape, never its words");

    message_view_dispose(&view);
    message_dispose(&msgs[0]);
    message_dispose(&msgs[1]);
    endwin();
    delscreen(screen);
    fclose(out);
    fclose(in);
}

int main(void) {
    char dir[] = "/tmp/tawk-transcripts-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char db_path[600];
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", dir);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) return 1;
    sqlite3_exec(db, "INSERT INTO accounts (id, label) VALUES (2, 'second');", NULL, NULL, NULL);

    test_engines();
    test_store(db);
    test_manager(db);
    test_conversation();

    sqlite_database_close(db);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: transcripts are kept per message and language, go with their message, show as you chose, and a chat switched off keeps what it had\n");
    return 0;
}
