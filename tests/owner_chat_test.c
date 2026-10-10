/* The owner's chat: which chat it is, whose words a message there is, how
 * a request's card reads and is answered, and what tawk remembers sending. */
#include "core/approval_card.h"
#include "core/message.h"
#include "core/settings.h"
#include "engines/approval_card_text.h"
#include "engines/approval_reply_parser.h"
#include "engines/owner_message_rule.h"
#include "engines/owner_reply_rule.h"
#include "engines/remote_approval_policy.h"
#include "engines/self_chat_rule.h"
#include "managers/owner_chat_manager.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_sent_id_log.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); failures++; } } while (0)

#define ME   "27821234567@s.whatsapp.net"
#define MINE "27821234567:14@s.whatsapp.net"

static void rules(void) {
    CHECK(self_chat_rule_is(MINE, ME) && self_chat_rule_is(ME, ME), "your own chat is the one with your own number, whichever device you are");
    CHECK(!self_chat_rule_is(MINE, "27829999999@s.whatsapp.net") && !self_chat_rule_is(MINE, "2782123456@s.whatsapp.net"),
          "another number is not, nor one that only starts the same");
    CHECK(!self_chat_rule_is(MINE, "27821234567-1500000000@g.us") && !self_chat_rule_is(MINE, "27821234567@g.us") && !self_chat_rule_is("", ME),
          "a group is never your own chat, and with no number of your own nothing is");

    Message m;
    message_init(&m);
    m.from_me = 1;
    m.type = MESSAGE_TYPE_TEXT;
    message_set_text(&m, "what did I miss today?");
    CHECK(owner_message_rule_kind(&m, 0) == OWNER_MESSAGE_WORDS, "text you typed there is your words");
    CHECK(owner_message_rule_kind(&m, 1) == OWNER_MESSAGE_NOT, "the same text sent by tawk is not: that is the agent's answer");
    m.forwarded = 1;
    CHECK(owner_message_rule_kind(&m, 0) == OWNER_MESSAGE_DATA, "a forwarded message is data, whatever it says");
    m.forwarded = 0;
    str_copy(m.quoted_id, sizeof(m.quoted_id), "3EB0AA");
    CHECK(owner_message_rule_kind(&m, 0) == OWNER_MESSAGE_DATA, "and so is one that quotes another");
    m.quoted_id[0] = '\0';
    m.type = MESSAGE_TYPE_AUDIO;
    CHECK(owner_message_rule_kind(&m, 0) == OWNER_MESSAGE_DATA, "a voice note is data: its transcript is never your words");
    m.type = MESSAGE_TYPE_TEXT;
    m.from_me = 0;
    CHECK(owner_message_rule_kind(&m, 0) == OWNER_MESSAGE_NOT, "and what is not from you is nothing at all");
    message_dispose(&m);

    CHECK(approval_reply_parse("y") == APPROVAL_REPLY_ALLOW && approval_reply_parse(" Yes. ") == APPROVAL_REPLY_ALLOW &&
          approval_reply_parse("ja") == APPROVAL_REPLY_ALLOW && approval_reply_parse("\xF0\x9F\x91\x8D") == APPROVAL_REPLY_ALLOW &&
          approval_reply_parse("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == APPROVAL_REPLY_ALLOW,
          "y, yes, ja and a thumbs up of any colour allow");
    CHECK(approval_reply_parse("n") == APPROVAL_REPLY_DECLINE && approval_reply_parse("No!") == APPROVAL_REPLY_DECLINE &&
          approval_reply_parse("nee") == APPROVAL_REPLY_DECLINE && approval_reply_parse("\xF0\x9F\x91\x8E") == APPROVAL_REPLY_DECLINE,
          "n, no, nee and a thumbs down decline");
    CHECK(approval_reply_parse("yes but say Friday") == APPROVAL_REPLY_EDIT && approval_reply_parse("See you at six") == APPROVAL_REPLY_EDIT &&
          approval_reply_parse("no thanks, tell her tomorrow") == APPROVAL_REPLY_EDIT,
          "any other words are the text to send instead, even when they start with yes or no");
    CHECK(approval_reply_parse("") == APPROVAL_REPLY_NONE && approval_reply_parse(NULL) == APPROVAL_REPLY_NONE && approval_reply_parse("   ") == APPROVAL_REPLY_NONE,
          "nothing answers nothing");
    CHECK(approval_reply_reaction("\xF0\x9F\x91\x8D") == APPROVAL_REPLY_ALLOW && approval_reply_reaction("\xF0\x9F\x91\x8E") == APPROVAL_REPLY_DECLINE &&
          approval_reply_reaction("\xE2\x9D\xA4") == APPROVAL_REPLY_NONE && approval_reply_reaction("") == APPROVAL_REPLY_NONE,
          "a thumb on the card answers it; a heart, or a reaction taken back, does not");

    CHECK(remote_approval_policy_offers(WRITE_KIND_SEND, 0, 1, 0), "a send to a chat you have is put to you on WhatsApp");
    CHECK(!remote_approval_policy_offers(WRITE_KIND_DESTRUCTIVE, 0, 1, 0) && !remote_approval_policy_offers(WRITE_KIND_MANAGE, 0, 1, 0),
          "a delete, a block or a change of settings never is");
    CHECK(!remote_approval_policy_offers(WRITE_KIND_SEND, 1, 1, 0), "nor a first message to someone new");
    CHECK(!remote_approval_policy_offers(WRITE_KIND_SEND, 0, 0, 0), "nor anything in a chat agents may not see");
    CHECK(!remote_approval_policy_due(1000, 20000, 20000) && remote_approval_policy_due(1000, 21000, 20000) && remote_approval_policy_due(1000, 1000, 0),
          "and only once it has waited in tawk as long as you said");

    int64_t sent[3] = { 0, 3500000, 3590000 };
    CHECK(owner_reply_rule_room(sent, 3, 3, 3600000 + 1) && !owner_reply_rule_room(sent, 3, 2, 3600000 + 1) && !owner_reply_rule_room(sent, 0, 0, 5),
          "an hour's answers are counted, and those from more than an hour back are not");
    CHECK(owner_reply_rule_covers("send_message") && !owner_reply_rule_covers("delete_message") && !owner_reply_rule_covers("schedule_message") &&
          !owner_reply_rule_covers(NULL), "only a plain message goes out unasked there");

    char text[4096];
    ApprovalCard card = { "tawk-mcp", "send a message", "Mom", "Personal", "On my way", 1, 0, 3 };
    approval_card_text(&card, text, sizeof(text));
    CHECK(strstr(text, "tawk: tawk-mcp wants to send a message in Mom (from Personal):") == text && strstr(text, "\n\nOn my way\n\n") &&
          strstr(text, "with y to allow it or n to decline it") && strstr(text, "other words") && strstr(text, "in 3 minutes."),
          "a card says who asks, what for, where, from which number, the exact words, and how to answer");
    ApprovalCard fixed = { "", "react with a heart", "", "", NULL, 0, 1, 1 };
    approval_card_text(&fixed, text, sizeof(text));
    CHECK(strstr(text, "tawk: changed. An agent wants to react with a heart\n\n") == text && !strstr(text, "other words") && strstr(text, "in 1 minute."),
          "one with no words offers no change of words, and a changed one says so");
    static char big[8000];
    memset(big, 'a', sizeof(big) - 1);
    ApprovalCard longer = { "x", "send a message", "Mom", "", big, 1, 0, 2 };
    approval_card_text(&longer, text, sizeof(text));
    CHECK(strlen(text) < sizeof(text) - 1 && strstr(text, "[cut here; tawk shows all of it]") && strstr(text, "with y to allow"),
          "very long words are cut with a mark, and the way to answer is still there");
}

static void remembered(void) {
    char db_path[256];
    snprintf(db_path, sizeof(db_path), "/tmp/tawk_owner_chat_test_%d.db", (int)getpid());
    remove(db_path);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    ISentIdLog *log = db ? sqlite_sent_id_log_create(db) : NULL;
    CHECK(log != NULL, "the log opens over the database");
    if (!log) return;
    Settings settings;
    settings_set_defaults(&settings);
    OwnerChatManagerDeps deps = { log, &settings };
    OwnerChatManager *mgr = owner_chat_manager_create(&deps);
    CHECK(owner_chat_manager_account(mgr) == ACCOUNT_ID_NONE && !owner_chat_manager_is(mgr, 2, MINE, ME), "until you name one, there is no owner's chat");
    str_copy(settings.owner_chat, sizeof(settings.owner_chat), "2");
    CHECK(owner_chat_manager_account(mgr) == 2 && owner_chat_manager_is(mgr, 2, MINE, ME), "named, it is that account's chat with yourself");
    CHECK(!owner_chat_manager_is(mgr, 1, MINE, ME) && !owner_chat_manager_is(mgr, 2, MINE, "27829999999@s.whatsapp.net"),
          "and not the same chat in another account, or any other chat in that one");

    SentKind kind = SENT_KIND_REPLY;
    int ref = 0;
    CHECK(!owner_chat_manager_sent(mgr, 2, "3EB0CARD", &kind, &ref), "a message tawk did not send is not remembered");
    owner_chat_manager_note_sent(mgr, 2, "3EB0CARD", SENT_KIND_CARD, 7);
    owner_chat_manager_note_sent(mgr, 2, "3EB0REPLY", SENT_KIND_REPLY, 0);
    CHECK(owner_chat_manager_sent(mgr, 2, "3EB0CARD", &kind, &ref) && kind == SENT_KIND_CARD && ref == 7, "a card is remembered with the request it is for");
    CHECK(!owner_chat_manager_sent(mgr, 1, "3EB0CARD", &kind, &ref), "for its own account only");

    Message m;
    message_init(&m);
    m.from_me = 1;
    m.type = MESSAGE_TYPE_TEXT;
    message_set_text(&m, "Here is what you missed");
    str_copy(m.id, sizeof(m.id), "3EB0REPLY");
    CHECK(owner_chat_manager_kind(mgr, 2, &m) == OWNER_MESSAGE_NOT, "the agent's answer coming back is not read as your words");
    str_copy(m.id, sizeof(m.id), "PHONE1");
    CHECK(owner_chat_manager_kind(mgr, 2, &m) == OWNER_MESSAGE_WORDS, "the same words typed on your phone are");
    message_dispose(&m);

    /* Another manager over the same database stands for tawk started again. */
    OwnerChatManager *again = owner_chat_manager_create(&deps);
    CHECK(owner_chat_manager_sent(again, 2, "3EB0REPLY", NULL, NULL), "what tawk sent is still known after a restart");
    owner_chat_manager_destroy(again);

    settings.owner_replies_per_hour = 2;
    CHECK(owner_chat_manager_take_reply(mgr, 1000) && owner_chat_manager_take_reply(mgr, 2000) && !owner_chat_manager_take_reply(mgr, 3000),
          "answers go out by themselves up to the hour's limit");
    CHECK(owner_chat_manager_take_reply(mgr, 1000 + 3600000), "and again once the hour has moved on");
    CHECK(owner_chat_manager_card_wait_ms(mgr) == 20000, "requests wait 20 seconds in tawk before they are put to you on WhatsApp");
    settings.owner_approvals = 0;
    CHECK(owner_chat_manager_card_wait_ms(mgr) < 0, "and never are when you switched that off");

    owner_chat_manager_destroy(mgr);
    log->destroy(log);
    sqlite_database_close(db);
    remove(db_path);
}

int main(void) {
    rules();
    remembered();
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("ok: owner's chat\n");
    return 0;
}
