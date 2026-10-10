/* TL;DR: which messages are summarised, what may be kept as a summary, the
 * store (one per message, gone when the message goes or changes), the list
 * of messages waiting for one, which agent writes them, the question tawk
 * asks when that is not clear, and folding a message open and shut. */
#include "clients/tui/message_view.h"
#include "clients/tui/tui_palette.h"
#include "contracts/i_message_store.h"
#include "engines/agent_question.h"
#include "engines/client_version.h"
#include "engines/summariser_choice.h"
#include "engines/summary_policy.h"
#include "engines/summary_validator.h"
#include "managers/summary_manager.h"
#include "resource_access/sqlite_chat_prefs_store.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_summary_store.h"
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

static const char LONG_TEXT[] =
    "Good morning. I spoke to the school this morning and they have moved the parents evening from Tuesday to "
    "Thursday because the hall is being used for the exams. They also asked whether we can bring something for the "
    "tea table, and whether one of us can help to pack up afterwards, which should not take more than half an hour. "
    "Please let me know before lunch so that I can answer them today. finalwords";

static void text_message(Message *m, const char *id, const char *jid, const char *text) {
    message_init(m);
    str_copy(m->id, sizeof(m->id), id);
    str_copy(m->chat_jid, sizeof(m->chat_jid), jid);
    str_copy(m->sender_jid, sizeof(m->sender_jid), jid);
    m->type = MESSAGE_TYPE_TEXT;
    m->timestamp = 1790000000;
    message_set_text(m, text);
}

static void test_engines(void) {
    Message longer, shorter, voice;
    text_message(&longer, "L1", MOM, LONG_TEXT);
    text_message(&shorter, "S1", MOM, "See you at 6");
    text_message(&voice, "V1", MOM, NULL);
    voice.type = MESSAGE_TYPE_AUDIO;
    Chat chat;
    chat_init(&chat, MOM);
    chat.is_locked = 0;
    ChatPrefs prefs;
    memset(&prefs, 0, sizeof(prefs));

    CHECK(!summary_policy_allows(&chat, &prefs) && !summary_policy_wants(&chat, &prefs, &longer, 300), "a chat is not in TL;DR mode until you switch it on");
    prefs.tldr = 1;
    CHECK(summary_policy_allows(&chat, &prefs) && summary_policy_wants(&chat, &prefs, &longer, 300), "switched on, a long message is one to summarise");
    CHECK(!summary_policy_wants(&chat, &prefs, &shorter, 300) && !summary_policy_wants(&chat, &prefs, &voice, 300), "a short one and a voice note are not");
    CHECK(!summary_policy_wants(&chat, &prefs, &longer, 5000), "the length is yours to set");
    CHECK(summary_policy_wants(&chat, &prefs, &shorter, 0) && summary_policy_wants(&chat, &prefs, &longer, 0) &&
          !summary_policy_wants(&chat, &prefs, &voice, 0), "set to 0, every text message is one to summarise");
    CHECK(summary_policy_shorter(LONG_TEXT, "Evening moved to Thursday.") && !summary_policy_shorter("See you at 6", "She will see you at six o'clock") &&
          !summary_policy_shorter("ok", "ok") && !summary_policy_shorter("ok", ""), "a summary is worth showing only when it is shorter than the message");
    longer.deleted = 1;
    CHECK(!summary_policy_wants(&chat, &prefs, &longer, 300), "nor a deleted message");
    longer.deleted = 0;
    chat.soft_locked = 1;
    CHECK(!summary_policy_allows(&chat, &prefs), "never a soft-locked chat");
    chat.soft_locked = 0;
    chat.is_locked = 1;
    CHECK(!summary_policy_allows(&chat, &prefs) && !summary_policy_allows(NULL, &prefs), "never a locked one, or none");

    CHECK(summary_validator_refusal(&longer, "School moved the evening to Thursday.") == NULL, "a text message takes a summary");
    CHECK(summary_validator_refusal(&voice, "x") != NULL && summary_validator_refusal(&longer, " \n") != NULL &&
          summary_validator_refusal(&longer, NULL) != NULL, "a voice note does not, and words are required");
    char *big = malloc(SUMMARY_MAX_BYTES + 2);
    memset(big, 'a', SUMMARY_MAX_BYTES + 1);
    big[SUMMARY_MAX_BYTES + 1] = '\0';
    CHECK(summary_validator_refusal(&longer, big) != NULL, "a summary has a size limit");
    free(big);
    char dirty[] = "  Evening moved\nto\tThursday.\x1b[31m   Bring tea.  ";
    summary_validator_clean(dirty);
    CHECK(strcmp(dirty, "Evening moved to Thursday. [31m Bring tea.") == 0, "a summary becomes one clean paragraph");

    SummariserCandidate one[] = { { 7, 0 } };
    SummariserCandidate two[] = { { 7, 0 }, { 9, 0 } };
    SummariserCandidate picked[] = { { 7, 0 }, { 9, 1 } };
    int conn = -1;
    CHECK(summariser_choice_pick(NULL, 0, &conn) == SUMMARISER_NONE, "with no agent connected nobody writes summaries");
    CHECK(summariser_choice_pick(one, 1, &conn) == SUMMARISER_USE && conn == 7, "the only agent connected is the one");
    CHECK(summariser_choice_pick(two, 2, &conn) == SUMMARISER_ASK, "several and none chosen: you are asked");
    CHECK(summariser_choice_pick(picked, 2, &conn) == SUMMARISER_USE && conn == 9, "the one you chose is the one");

    AutomationSession agents[2];
    memset(agents, 0, sizeof(agents));
    str_copy(agents[0].client, sizeof(agents[0].client), "tawk-mcp");
    str_copy(agents[0].label, sizeof(agents[0].label), "dev (stdio, pid 3002)");
    str_copy(agents[0].doing, sizeof(agents[0].doing), "Reviewing the billing service");
    str_copy(agents[1].client, sizeof(agents[1].client), "tawk-mcp");
    str_copy(agents[1].label, sizeof(agents[1].label), "home (http)");
    char question[600];
    agent_question_text(agents, 2, question, sizeof(question));
    CHECK(strstr(question, "1. tawk-mcp, dev (stdio, pid 3002): Reviewing the billing service") && strstr(question, "2. tawk-mcp, home (http)"),
          "the question lists each agent with what it says it is doing");
    CHECK(agent_question_answer("2", 2) == 1 && agent_question_answer(" 1. ", 2) == 0 && agent_question_answer("2\n", 2) == 1, "a number answers it");
    CHECK(agent_question_answer("3", 2) == -1 && agent_question_answer("0", 2) == -1 && agent_question_answer("2 please", 2) == -1 &&
          agent_question_answer("two", 2) == -1 && agent_question_answer("", 2) == -1 && agent_question_answer(question, 2) == -1,
          "anything else does not, tawk's own question included");
    char key[64];
    agent_question_label_key("dev (stdio, pid 3002)", key, sizeof(key));
    CHECK(strcmp(key, "dev (stdio)") == 0, "an agent is remembered by its label without the process id");
    agent_question_label_key("home (http)", key, sizeof(key));
    CHECK(strcmp(key, "home (http)") == 0, "a label without one stays as it is");
    CHECK(client_version_compare("0.10.2", "0.10.1") > 0 && client_version_compare("0.9.9", "0.10.0") < 0 &&
          client_version_compare("0.10.2", "0.10.2") == 0 && client_version_compare("1.0.0", "0.99.9") > 0,
          "the newer of two versions is told by its numbers, not its letters");
    CHECK(client_version_compare("0.10.2", "") > 0 && client_version_compare(NULL, "0.1.0") < 0 && client_version_compare("", NULL) == 0,
          "and a client that gives no version is older than one that does");
    message_dispose(&longer);
    message_dispose(&shorter);
    message_dispose(&voice);
}

static int has_summary(ISummaryStore *store, const char *id) {
    Summary s;
    if (store->find(store, id, &s) != 0) return 0;
    summary_dispose(&s);
    return 1;
}

static void test_store_and_manager(sqlite3 *db) {
    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    ISummaryStore *store = sqlite_summary_store_create(db, ACCOUNT_ID_FIRST);
    ISummaryStore *other = sqlite_summary_store_create(db, ACCOUNT_ID_FIRST + 1);
    IChatPrefsStore *prefs = sqlite_chat_prefs_store_create(db);
    Settings settings;
    settings_set_defaults(&settings);
    CHECK(settings.tldr_min_chars == 0, "out of the box, every message of a TL;DR chat is summarised");
    settings.tldr_min_chars = 300;                          /* the checks below are about a length you set */
    SummaryManagerDeps deps = { store, prefs, sqlite_chat_prefs_store_summaries(prefs), &settings };
    SummaryManager *mgr = summary_manager_create(&deps);
    SummaryManagerDeps missing = { store, prefs, NULL, &settings };
    CHECK(mgr != NULL && summary_manager_create(&missing) == NULL, "the manager needs every part");

    Chat mom;
    chat_init(&mom, MOM);
    mom.is_locked = 0;
    Message a, b, c, shorter;
    text_message(&a, "L1", MOM, LONG_TEXT);
    text_message(&b, "L2", MOM, LONG_TEXT);
    text_message(&c, "L3", MOM, LONG_TEXT);
    text_message(&shorter, "S1", MOM, "See you at 6");
    messages->save(messages, &a);
    messages->save(messages, &b);
    messages->save(messages, &c);

    CHECK(!summary_manager_tldr(mgr, MOM), "a chat starts out of TL;DR mode");
    CHECK(summary_manager_save(mgr, &a, &mom, "Evening moved to Thursday.", "m", "tawk-mcp") == SUMMARY_OFF, "and takes no summary then");
    char id[64];
    summary_manager_want(mgr, &a, &mom);
    CHECK(!summary_manager_next_wanted(mgr, id, sizeof(id)), "nor is anything asked for");

    CHECK(summary_manager_set_tldr(mgr, MOM, 1) == 0 && summary_manager_tldr(mgr, MOM) && !summary_manager_tldr(mgr, WORK), "one chat is switched on");
    CHECK(summary_manager_take_changed(mgr) && !summary_manager_take_changed(mgr), "the change is reported once");
    CHECK(summary_manager_wants(mgr, &a, &mom) && !summary_manager_wants(mgr, &shorter, &mom), "its long messages are wanted, its short ones not");
    summary_manager_want(mgr, &a, &mom);
    summary_manager_want(mgr, &shorter, &mom);
    summary_manager_want(mgr, &b, &mom);
    summary_manager_want(mgr, &a, &mom);
    CHECK(summary_manager_next_wanted(mgr, id, sizeof(id)) && strcmp(id, "L1") == 0, "the one that waited longest comes first");
    summary_manager_drop_wanted(mgr);
    CHECK(summary_manager_next_wanted(mgr, id, sizeof(id)) && strcmp(id, "L2") == 0, "then the next");
    summary_manager_drop_wanted(mgr);
    CHECK(!summary_manager_next_wanted(mgr, id, sizeof(id)), "each is asked for once, however often it is looked at");

    CHECK(summary_manager_save(mgr, &a, &mom, "  Evening moved\nto Thursday. ", "sonnet", "tawk-mcp") == SUMMARY_SAVED, "a summary is kept");
    Summary got;
    CHECK(summary_manager_find(mgr, "L1", &got) == 0 && strcmp(got.text, "Evening moved to Thursday.") == 0 &&
          strcmp(got.model, "sonnet") == 0 && strcmp(got.source, "tawk-mcp") == 0, "cleaned, with who wrote it");
    summary_dispose(&got);
    CHECK(summary_manager_save(mgr, &a, &mom, "Thursday now.", "sonnet", "tawk-mcp") == SUMMARY_SAVED &&
          summary_manager_find(mgr, "L1", &got) == 0 && strcmp(got.text, "Thursday now.") == 0, "a second one replaces the first");
    summary_dispose(&got);
    summary_manager_want(mgr, &c, &mom);
    summary_manager_save(mgr, &c, &mom, "Same again.", "", "tawk-mcp");
    CHECK(!has_summary(other, "L1"), "another account sees none of them");

    CHECK(summary_manager_set_tldr(mgr, MOM, 0) == 0 && has_summary(store, "L1"), "switching TL;DR off keeps the summaries");
    summary_manager_set_tldr(mgr, MOM, 1);
    CHECK(messages->edit_text(messages, "L1", "Changed my mind", 0) == 0 && !has_summary(store, "L1"), "an edited message loses its summary, which no longer fits");
    summary_manager_save(mgr, &b, &mom, "Evening moved.", "", "tawk-mcp");
    CHECK(messages->edit_text(messages, "L2", NULL, 1) == 0 && !has_summary(store, "L2"), "so does one deleted for everyone");
    CHECK(has_summary(store, "L3") && messages->remove(messages, "L3") == 0 && !has_summary(store, "L3"), "and one deleted here");
    mom.soft_locked = 1;
    CHECK(summary_manager_save(mgr, &a, &mom, "x", "", "") == SUMMARY_OFF, "a soft-locked chat takes none");
    mom.soft_locked = 0;

    /* the older messages of a TL;DR chat: the last month, newest first, once */
    SummaryManagerDeps with_history = { store, prefs, sqlite_chat_prefs_store_summaries(prefs), &settings, messages };
    SummaryManager *filler = summary_manager_create(&with_history);
    Chat work;
    chat_init(&work, WORK);
    work.is_locked = 0;
    const int64_t now = 1790000000 + 86400;
    Message old;
    text_message(&old, "D40", WORK, LONG_TEXT);
    old.timestamp = now - 40 * 86400;
    messages->save(messages, &old);
    message_dispose(&old);
    text_message(&old, "D10", WORK, LONG_TEXT);
    old.timestamp = now - 10 * 86400;
    messages->save(messages, &old);
    message_dispose(&old);
    text_message(&old, "D02", WORK, LONG_TEXT);
    old.timestamp = now - 2 * 86400;
    messages->save(messages, &old);
    message_dispose(&old);
    text_message(&old, "D01mine", WORK, LONG_TEXT);
    old.timestamp = now - 86400;
    old.from_me = 1;
    messages->save(messages, &old);
    message_dispose(&old);
    text_message(&old, "D01short", WORK, "ok thanks");
    old.timestamp = now - 86400;
    messages->save(messages, &old);
    message_dispose(&old);
    CHECK(summary_manager_backfill(filler, &work, now) == 0, "a chat that is not in TL;DR mode has nothing filled in");
    summary_manager_set_tldr(filler, WORK, 1);
    CHECK(summary_manager_backfill(filler, &work, now) == 3, "switched on, its long messages of the last month wait for a summary, yours as well as theirs");
    CHECK(summary_manager_next_wanted(filler, id, sizeof(id)) && strcmp(id, "D01mine") == 0, "the newest first, and that one is your own");
    summary_manager_drop_wanted(filler);
    CHECK(summary_manager_next_wanted(filler, id, sizeof(id)) && strcmp(id, "D02") == 0, "then the one before");
    summary_manager_drop_wanted(filler);
    CHECK(summary_manager_next_wanted(filler, id, sizeof(id)) && strcmp(id, "D10") == 0, "and the one before that");
    summary_manager_drop_wanted(filler);
    CHECK(!summary_manager_next_wanted(filler, id, sizeof(id)), "not the one from more than a month back, or a short one");
    CHECK(summary_manager_backfill(filler, &work, now) == 0, "and it is done once");
    settings.tldr_back_days = 60;
    summary_manager_set_tldr(filler, WORK, 0);
    summary_manager_set_tldr(filler, WORK, 1);
    CHECK(summary_manager_backfill(filler, &work, now) == 1 && summary_manager_next_wanted(filler, id, sizeof(id)) && strcmp(id, "D40") == 0,
          "looking further back after switching it on again adds only what was not asked for");
    settings.tldr_back_days = 0;
    summary_manager_set_tldr(filler, WORK, 0);
    summary_manager_set_tldr(filler, WORK, 1);
    CHECK(summary_manager_backfill(filler, &work, now) == 0, "set to 0, only what you look at is summarised");
    settings.tldr_back_days = 30;
    summary_manager_destroy(filler);

    message_dispose(&a);
    message_dispose(&b);
    message_dispose(&c);
    message_dispose(&shorter);
    summary_manager_destroy(mgr);
    prefs->destroy(prefs);
    other->destroy(other);
    store->destroy(store);
    messages->destroy(messages);
}

/* ---- the conversation, drawn on a screen nobody sees ------------------------- */

static int fake_find(void *ctx, const Message *message, AccountId owner, Summary *out) {
    (void)owner;
    if (strcmp(message->id, "L1") != 0) return -1;
    summary_set_text(out, (const char *)ctx);
    return 0;
}

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
    text_message(&msgs[0], "L1", MOM, LONG_TEXT);
    text_message(&msgs[1], "S1", MOM, "See you at 6");
    char words[] = "Parents evening moved to Thursday.";
    SummarySource source = { words, fake_find };
    MessageViewContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.title = "Mom";
    ctx.playing_path = "";
    UiRect rect = { 0, 0, 40, 100 };
    MessageView view;
    message_view_init(&view);

    erase();
    message_view_render(&view, rect, msgs, 2, &ctx);
    int rows_plain = view.row_count;
    CHECK(on_screen("finalwords") && !on_screen("TL;DR") && !message_view_summarised(&view, 0), "out of TL;DR mode a long message shows in full");

    erase();
    ctx.summaries = &source;
    message_view_render(&view, rect, msgs, 2, &ctx);
    CHECK(on_screen("TL;DR") && on_screen("Parents evening moved to Thursday.") && !on_screen("finalwords"),
          "in TL;DR mode it shows its summary in place of the text");
    CHECK(view.row_count < rows_plain && message_view_summarised(&view, 0) && !message_view_summarised(&view, 1) && on_screen("See you at 6"),
          "which takes less room, and a short message is left as it is");

    erase();
    message_view_toggle_summary(&view, "L1");
    message_view_render(&view, rect, msgs, 2, &ctx);
    CHECK(message_view_unfolded(&view, "L1") && on_screen("finalwords") && on_screen("original") && !on_screen("Parents evening moved"),
          "unfolded, it shows the original under the same line");

    erase();
    message_view_toggle_summary(&view, "L1");
    message_view_render(&view, rect, msgs, 2, &ctx);
    CHECK(!message_view_unfolded(&view, "L1") && on_screen("Parents evening moved to Thursday.") && !on_screen("finalwords"),
          "and folds back to the summary");

    erase();
    ctx.veiled = 1;
    message_view_render(&view, rect, msgs, 2, &ctx);
    CHECK(!on_screen("Parents evening") && !on_screen("finalwords"), "a soft-locked chat shows neither");

    message_view_dispose(&view);
    message_dispose(&msgs[0]);
    message_dispose(&msgs[1]);
    endwin();
    delscreen(screen);
    fclose(out);
    fclose(in);
}

int main(void) {
    char dir[] = "/tmp/tawk-summaries-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char db_path[600];
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", dir);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) return 1;

    test_engines();
    test_store_and_manager(db);
    test_conversation();

    sqlite_database_close(db);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: long messages in a TL;DR chat show as a summary that unfolds to the original, written by the agent you chose\n");
    return 0;
}
