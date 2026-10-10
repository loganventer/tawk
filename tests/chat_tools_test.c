/* Tools for a long chat list: labels, chats put aside with a reminder,
 * narrowing the list, and the chats awaiting a reply. */
#include "clients/tui/chat_list_view.h"
#include "core/chat.h"
#include "core/message.h"
#include "engines/chat_filter.h"
#include "engines/label_name.h"
#include "engines/reminder_rule.h"
#include "managers/label_manager.h"
#include "managers/reminder_manager.h"
#include "resource_access/sqlite_awaiting_replies.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_label_store.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_reminder_store.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); failures++; } } while (0)

#define MOM  "27820000001@s.whatsapp.net"
#define WORK "27820000002@s.whatsapp.net"
#define CLUB "27820000003-1500000000@g.us"

static void rules(void) {
    char clean[CHAT_LABEL_SIZE];
    CHECK(label_name_clean("  Work Stuff ", clean, sizeof(clean)) == 0 && !strcmp(clean, "work stuff"), "a label is trimmed and kept in lower case");
    CHECK(label_name_clean("", clean, sizeof(clean)) != 0 && label_name_clean("   ", clean, sizeof(clean)) != 0 &&
          label_name_clean("a,b", clean, sizeof(clean)) != 0 && label_name_clean("a/b", clean, sizeof(clean)) != 0 &&
          label_name_clean("abcdefghijklmnopqrstuvwxyz", clean, sizeof(clean)) != 0 && label_name_clean("tab\there", clean, sizeof(clean)) != 0,
          "an empty one, one with a comma, a slash or a tab, and one over 24 characters are not labels");

    ChatFilterKind kind = CHAT_FILTER_UNREAD;
    char label[CHAT_LABEL_SIZE];
    CHECK(chat_filter_parse("", &kind, label) == 0 && kind == CHAT_FILTER_NONE && chat_filter_parse("off", &kind, label) == 0 && kind == CHAT_FILTER_NONE,
          "nothing, and off, mean every chat");
    CHECK(chat_filter_parse("Unread", &kind, label) == 0 && kind == CHAT_FILTER_UNREAD && chat_filter_parse(" groups ", &kind, label) == 0 && kind == CHAT_FILTER_GROUPS &&
          chat_filter_parse("direct", &kind, label) == 0 && kind == CHAT_FILTER_DIRECT && chat_filter_parse("awaiting", &kind, label) == 0 && kind == CHAT_FILTER_AWAITING &&
          chat_filter_parse("snoozed", &kind, label) == 0 && kind == CHAT_FILTER_SNOOZED, "each kind is read, in any case");
    CHECK(chat_filter_parse("label Work", &kind, label) == 0 && kind == CHAT_FILTER_LABEL && !strcmp(label, "work"), "a label is read with its name");
    CHECK(chat_filter_parse("label", &kind, label) != 0 && chat_filter_parse("unreadable", &kind, label) != 0 && chat_filter_parse("unread now", &kind, label) != 0,
          "a label with no name, and words that are not a kind, are refused");
    char title[64];
    chat_filter_title(CHAT_FILTER_LABEL, "work", title, sizeof(title));
    CHECK(!strcmp(title, "label work"), "a narrowed list says what it shows");
    chat_filter_title(CHAT_FILTER_NONE, "", title, sizeof(title));
    CHECK(title[0] == '\0', "and says nothing when it shows every chat");

    Chat direct, group;
    chat_init(&direct, MOM);
    chat_init(&group, CLUB);
    group.is_group = 1;
    group.unread = 2;
    ChatFilterFacts none = { 0, 0, 0 }, snoozed = { 0, 1, 0 }, awaiting = { 1, 0, 0 }, labelled = { 0, 0, 1 };
    CHECK(chat_filter_shows(CHAT_FILTER_NONE, &direct, &none) && !chat_filter_shows(CHAT_FILTER_NONE, &direct, &snoozed), "with no filter a chat put aside is left out");
    CHECK(chat_filter_shows(CHAT_FILTER_SNOOZED, &direct, &snoozed) && !chat_filter_shows(CHAT_FILTER_SNOOZED, &direct, &none), "and it is what snoozed shows");
    CHECK(chat_filter_shows(CHAT_FILTER_UNREAD, &group, &none) && !chat_filter_shows(CHAT_FILTER_UNREAD, &direct, &none), "unread shows chats with unread messages");
    CHECK(chat_filter_shows(CHAT_FILTER_GROUPS, &group, &none) && !chat_filter_shows(CHAT_FILTER_GROUPS, &direct, &none) &&
          chat_filter_shows(CHAT_FILTER_DIRECT, &direct, &none) && !chat_filter_shows(CHAT_FILTER_DIRECT, &group, &none), "groups and direct chats are told apart");
    CHECK(chat_filter_shows(CHAT_FILTER_AWAITING, &direct, &awaiting) && !chat_filter_shows(CHAT_FILTER_AWAITING, &direct, &none) &&
          chat_filter_shows(CHAT_FILTER_LABEL, &direct, &labelled) && !chat_filter_shows(CHAT_FILTER_LABEL, &direct, &none), "awaiting and a label show the chats they are true of");

    int64_t now = 1790000000, due = -1;
    CHECK(reminder_rule_parse("reply", now, &due) == 0 && due == 0, "reply means no time: only when they write");
    CHECK(reminder_rule_parse("+2h", now, &due) == 0 && due == now + 7200, "a time is read as /later reads it");
    CHECK(reminder_rule_parse("", now, &due) != 0 && reminder_rule_parse("soon", now, &due) != 0 && reminder_rule_parse("+2h and more", now, &due) != 0 &&
          reminder_rule_parse("reply please", now, &due) != 0, "anything else is refused, and so are words after the time");
    ChatReminder timed = { MOM, now + 100, now }, open_ended = { MOM, 0, now };
    CHECK(!reminder_rule_due(&timed, now + 99, 0) && reminder_rule_due(&timed, now + 100, 0), "a chat comes back when its time comes");
    CHECK(reminder_rule_due(&timed, now, 1) && reminder_rule_due(&open_ended, now, 3), "or sooner, when its person writes");
    CHECK(!reminder_rule_due(&open_ended, now + 999999, 0), "one with no time waits for that alone");
    CHECK(reminder_rule_awaiting_before(now, 3) == now - 3 * 86400 && reminder_rule_awaiting_before(now, 0) == now - 86400, "days are counted back from now, one at least");
}

static void add(IMessageStore *messages, const char *id, const char *jid, int from_me, int64_t ts) {
    Message m;
    message_init(&m);
    str_copy(m.id, sizeof(m.id), id);
    str_copy(m.chat_jid, sizeof(m.chat_jid), jid);
    str_copy(m.sender_jid, sizeof(m.sender_jid), from_me ? "me@s.whatsapp.net" : jid);
    m.from_me = from_me;
    m.timestamp = ts;
    message_set_text(&m, "text");
    messages->save(messages, &m);
    message_dispose(&m);
}

static void kept(sqlite3 *db) {
    ILabelStore *label_store = sqlite_label_store_create(db);
    LabelManager *labels = label_manager_create(label_store);
    char clean[CHAT_LABEL_SIZE], of[160], all[LABELS_MAX][CHAT_LABEL_SIZE];
    CHECK(label_manager_toggle(labels, MOM, " Family ", clean) == 1 && !strcmp(clean, "family") && label_manager_has(labels, MOM, "family"), "a label goes on a chat");
    CHECK(label_manager_toggle(labels, MOM, "urgent", clean) == 1 && label_manager_toggle(labels, WORK, "urgent", clean) == 1, "a chat carries several, and a label several chats");
    label_manager_of(labels, MOM, of, sizeof(of));
    CHECK(!strcmp(of, "family, urgent"), "a chat's labels read as a list");
    CHECK(label_manager_all(labels, all, LABELS_MAX) == 2 && !strcmp(all[0], "family") && !strcmp(all[1], "urgent"), "each label is listed once");
    CHECK(label_manager_toggle(labels, MOM, "FAMILY", clean) == 0 && !label_manager_has(labels, MOM, "family"), "the same name again takes it off");
    CHECK(label_manager_all(labels, all, LABELS_MAX) == 1, "and a label no chat carries is gone");
    CHECK(label_manager_toggle(labels, MOM, "a,b", clean) < 0, "a bad name is refused");
    label_manager_destroy(labels);
    labels = label_manager_create(label_store);
    CHECK(label_manager_has(labels, WORK, "urgent"), "labels are still there when tawk starts again");
    label_manager_destroy(labels);
    label_store->destroy(label_store);

    IReminderStore *reminder_store = sqlite_reminder_store_create(db);
    ReminderManager *reminders = reminder_manager_create(reminder_store);
    int64_t now = 1790000000, due = -1;
    CHECK(!reminder_manager_snoozed(reminders, MOM, NULL) && reminder_manager_count(reminders) == 0, "no chat is put aside to begin with");
    CHECK(reminder_manager_set(reminders, MOM, now + 60, now) == 0 && reminder_manager_snoozed(reminders, MOM, &due) && due == now + 60, "a chat is put aside until a time");
    CHECK(reminder_manager_set(reminders, MOM, now + 120, now) == 0 && reminder_manager_count(reminders) == 1, "setting it again moves the one reminder");
    CHECK(reminder_manager_set(reminders, WORK, 0, now) == 0 && reminder_manager_count(reminders) == 2, "another until its person writes");
    CHECK(!reminder_manager_take_due(reminders, MOM, 0, now + 119) && reminder_manager_take_due(reminders, MOM, 0, now + 120) && !reminder_manager_snoozed(reminders, MOM, NULL),
          "it comes back at its time, and the reminder is gone");
    CHECK(!reminder_manager_take_due(reminders, WORK, 0, now + 99999) && reminder_manager_take_due(reminders, WORK, 1, now), "the other comes back when a message arrives");
    reminder_manager_set(reminders, MOM, now + 60, now);
    reminder_manager_destroy(reminders);
    reminders = reminder_manager_create(reminder_store);
    CHECK(reminder_manager_snoozed(reminders, MOM, NULL), "a reminder is still there when tawk starts again");
    CHECK(reminder_manager_clear(reminders, MOM) == 0 && reminder_manager_count(reminders) == 0, "and can be taken off by hand");
    reminder_manager_destroy(reminders);
    reminder_store->destroy(reminder_store);

    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    IAwaitingReplies *awaiting = sqlite_awaiting_replies_create(db, ACCOUNT_ID_FIRST);
    int64_t day = 86400;
    add(messages, "A1", MOM, 0, now - 9 * day);
    add(messages, "A2", MOM, 1, now - 5 * day);             /* yours, five days old, unanswered */
    add(messages, "B1", WORK, 1, now - 5 * day);
    add(messages, "B2", WORK, 0, now - 4 * day);            /* they answered */
    add(messages, "C1", CLUB, 1, now - 5 * day);            /* a group */
    add(messages, "D1", "27820000004@s.whatsapp.net", 1, now - 1 * day);    /* too recent */
    add(messages, "E1", "27820000005@s.whatsapp.net", 1, now - 90 * day);   /* long ago */
    char jids[8][128];
    int n = awaiting->list(awaiting, now - 60 * day, now - 3 * day, jids, 8);
    CHECK(n == 1 && !strcmp(jids[0], MOM), "awaiting a reply: a one-to-one chat whose newest message is yours and old enough, not a group, an answered chat, a recent one or an ancient one");
    IAwaitingReplies *other = sqlite_awaiting_replies_create(db, 2);
    CHECK(other->list(other, now - 60 * day, now - 3 * day, jids, 8) == 0, "and only in the account asked about");
    other->destroy(other);
    awaiting->destroy(awaiting);
    messages->destroy(messages);
}

static int chat_rows(const ChatListView *v) {
    int n = 0;
    for (int i = 0; i < v->entry_count; i++) n += v->entries[i].kind == CHAT_LIST_ENTRY_CHAT;
    return n;
}

static void list(void) {
    Chat chats[3];
    chat_init(&chats[0], MOM);
    chat_init(&chats[1], WORK);
    chat_init(&chats[2], CLUB);
    static ChatListView v;
    chat_list_view_init(&v);
    chat_list_view_sync(&v, chats, 3);
    CHECK(chat_rows(&v) == 3 && v.entries[0].kind == CHAT_LIST_ENTRY_CHAT, "with nothing narrowed every chat is listed and there is no extra row");
    unsigned char hidden[3] = { 0, 1, 0 };
    v.hidden = hidden;
    v.hidden_count = 3;
    chat_list_view_sync(&v, chats, 3);
    CHECK(chat_rows(&v) == 2 && v.entries[0].kind == CHAT_LIST_ENTRY_CHAT, "a chat put aside is left out, with no row to say so");
    str_copy(v.narrowed, sizeof(v.narrowed), "unread");
    chat_list_view_sync(&v, chats, 3);
    CHECK(chat_rows(&v) == 2 && v.entries[0].kind == CHAT_LIST_ENTRY_NARROWED, "a narrowed list starts with a row that says what it shows");
    v.selected = 0;
    CHECK(chat_list_view_activate(&v, chats, 3) == NULL && v.widen_asked, "Enter on that row asks for every chat again");
    str_copy(v.filter, sizeof(v.filter), "wo");
    str_copy(chats[1].name, sizeof(chats[1].name), "Work");
    chat_list_view_sync(&v, chats, 3);
    CHECK(chat_rows(&v) == 1 && v.entries[0].kind == CHAT_LIST_ENTRY_CHAT && v.entries[0].chat == 1, "typing in the search box finds a chat that was left out");
}

int main(void) {
    char db_path[256];
    snprintf(db_path, sizeof(db_path), "/tmp/tawk_chat_tools_test_%d.db", (int)getpid());
    remove(db_path);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) return 1;
    rules();
    kept(db);
    list();
    sqlite_database_close(db);
    remove(db_path);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("ok: labels, reminders, narrowing the chat list and awaiting a reply\n");
    return 0;
}
