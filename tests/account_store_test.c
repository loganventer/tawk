/* Several accounts in one database: stores made for different accounts do
 * not see each other's rows, the same message id can belong to each, the
 * roster names and orders the accounts, a contact's own choices span them,
 * and removing an account takes its rows and leaves the others whole. */
#include "engines/account_label_validator.h"
#include "managers/account_roster_manager.h"
#include "resource_access/sqlite_account_store.h"
#include "resource_access/sqlite_chat_prefs_store.h"
#include "resource_access/sqlite_chat_store.h"
#include "resource_access/sqlite_contact_store.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_jid_alias_store.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_profile_store.h"
#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_receipt_store.h"
#include "resource_access/sqlite_scheduled_message_store.h"
#include "resource_access/sqlite_status_store.h"
#include "utilities/str_util.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define MOM  "27820000001@s.whatsapp.net"
#define FAM  "120363000000000001@g.us"
#define NOW  1790000000

static int scalar(sqlite3 *db, const char *sql) {
    sqlite3_stmt *st = NULL;
    int v = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return v;
}

static void save_message(IMessageStore *s, const char *id, const char *chat, const char *text, int64_t ts) {
    Message m;
    message_init(&m);
    str_copy(m.id, sizeof(m.id), id);
    str_copy(m.chat_jid, sizeof(m.chat_jid), chat);
    str_copy(m.sender_jid, sizeof(m.sender_jid), chat);
    message_set_text(&m, text);
    m.timestamp = ts;
    CHECK(s->save(s, &m) == 0, "a message is saved");
    message_dispose(&m);
}

static int count_recent(IMessageStore *s, const char *chat) {
    Message *items = NULL;
    int n = 0;
    if (s->recent(s, chat, 50, &items, &n) != 0) return -1;
    for (int i = 0; i < n; i++) message_dispose(&items[i]);
    free(items);
    return n;
}

static int count_search(IMessageStore *s, const char *query) {
    Message *items = NULL;
    int n = 0;
    if (s->search(s, query, 50, &items, &n) != 0) return -1;
    for (int i = 0; i < n; i++) message_dispose(&items[i]);
    free(items);
    return n;
}

static void test_labels(void) {
    char why[160];
    CHECK(account_label_validate("work", why, sizeof(why)) == 0, "a plain label is fine");
    CHECK(account_label_validate("My AI number", why, sizeof(why)) == 0, "a label may have spaces");
    CHECK(account_label_validate("", why, sizeof(why)) != 0 && why[0], "an empty label is refused, with a reason");
    CHECK(account_label_validate("   ", why, sizeof(why)) != 0, "a label of spaces is refused");
    CHECK(account_label_validate("a,b", why, sizeof(why)) != 0, "a comma is refused: labels are listed with commas");
    CHECK(account_label_validate("two\nlines", why, sizeof(why)) != 0, "a line break is refused");
    CHECK(account_label_validate("abcdefghijklmnopqrstuvwx", why, sizeof(why)) == 0, "24 characters fit");
    CHECK(account_label_validate("abcdefghijklmnopqrstuvwxy", why, sizeof(why)) != 0, "25 do not");
    CHECK(account_label_validate("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
                                 "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9",
                                 why, sizeof(why)) == 0, "characters are counted, not bytes");
    CHECK(account_label_same("Work", " work ") && !account_label_same("work", "works"), "labels compare without case or outer spaces");
}

static void test_roster(sqlite3 *db) {
    IAccountStore *accounts = sqlite_account_store_create(db);
    IChatPrefsStore *prefs = sqlite_chat_prefs_store_create(db);
    AccountRosterManagerDeps deps = { accounts, prefs };
    AccountRosterManager *roster = account_roster_manager_create(&deps);

    Account all[ACCOUNT_MAX];
    CHECK(account_roster_manager_list(roster, all, ACCOUNT_MAX) == 1 && all[0].id == ACCOUNT_ID_FIRST && all[0].is_primary &&
          strcmp(all[0].label, "main") == 0 && all[0].agent_access == ACCOUNT_AGENT_FOLLOW,
          "a database starts with one account, primary, that follows the agent setting");
    CHECK(account_roster_manager_primary(roster) == ACCOUNT_ID_FIRST, "it is the primary one");

    AccountId work = ACCOUNT_ID_NONE;
    CHECK(account_roster_manager_add(roster, "  Work  ", NOW, &work) == 0 && work == 2, "a second account is added and gets the next id");
    Account second;
    CHECK(account_roster_manager_get(roster, work, &second) == 0 && strcmp(second.label, "Work") == 0 && !second.is_primary &&
          second.agent_access == ACCOUNT_AGENT_OFF && second.colour != all[0].colour,
          "it keeps its label trimmed, is not primary, is closed to agents and has its own colour");
    CHECK(account_roster_manager_take_changed(roster) && !account_roster_manager_take_changed(roster), "a change is reported once");

    CHECK(account_roster_manager_add(roster, "work", NOW, NULL) != 0 && account_roster_manager_error(roster)[0],
          "a label another account has is refused, whatever its case");
    CHECK(account_roster_manager_add(roster, "", NOW, NULL) != 0, "an empty label is refused");
    CHECK(account_roster_manager_rename(roster, work, "Main") != 0, "renaming onto another account's label is refused");
    CHECK(account_roster_manager_rename(roster, work, "work") == 0, "renaming an account to its own label in another case is allowed");
    CHECK(account_roster_manager_rename(roster, work, "AI number") == 0 && account_roster_manager_get(roster, work, &second) == 0 &&
          strcmp(second.label, "AI number") == 0, "an account is renamed");

    CHECK(account_roster_manager_set_agent_access(roster, work, ACCOUNT_AGENT_SEND) == 0 &&
          account_roster_manager_get(roster, work, &second) == 0 && second.agent_access == ACCOUNT_AGENT_SEND,
          "agent access is set for one account");
    CHECK(account_roster_manager_get(roster, ACCOUNT_ID_FIRST, &all[0]) == 0 && all[0].agent_access == ACCOUNT_AGENT_FOLLOW,
          "and the other keeps its own");

    CHECK(account_roster_manager_set_primary(roster, work) == 0 && account_roster_manager_primary(roster) == work &&
          scalar(db, "SELECT count(*) FROM accounts WHERE is_primary = 1") == 1, "one account at a time is primary");
    CHECK(account_roster_manager_set_primary(roster, 99) != 0 && account_roster_manager_primary(roster) == work,
          "making an account that does not exist primary changes nothing");

    CHECK(account_roster_manager_linked(roster, work, "27722173313@s.whatsapp.net", "Logan") == 0 &&
          account_roster_manager_get(roster, work, &second) == 0 && strcmp(second.jid, "27722173313@s.whatsapp.net") == 0 &&
          strcmp(second.name, "Logan") == 0, "an account remembers who it linked as");
    CHECK(account_roster_manager_linked(roster, work, "27722173313@s.whatsapp.net", "") == 0 &&
          account_roster_manager_get(roster, work, &second) == 0 && strcmp(second.name, "Logan") == 0,
          "linking again without a name keeps the name");

    char text[256];
    CHECK(account_roster_manager_set_last_chat(roster, work, MOM) == 0 &&
          account_roster_manager_last_chat(roster, work, text, sizeof(text)) == 0 && strcmp(text, MOM) == 0 &&
          account_roster_manager_last_chat(roster, ACCOUNT_ID_FIRST, text, sizeof(text)) == 0 && text[0] == '\0',
          "the last chat is kept for each account");
    CHECK(account_roster_manager_set_self_approval_chats(roster, work, MOM) == 0 &&
          account_roster_manager_self_approval_chats(roster, work, text, sizeof(text)) == 0 && strcmp(text, MOM) == 0 &&
          account_roster_manager_self_approval_chats(roster, ACCOUNT_ID_FIRST, text, sizeof(text)) == 0 && text[0] == '\0',
          "the chats an agent answers by itself are kept for each account");

    /* What you chose for one contact */
    ChatPrefs p;
    CHECK(account_roster_manager_chat_prefs(roster, MOM, &p) == 0 && p.send_account == ACCOUNT_ID_NONE && p.merge == CHAT_MERGE_FOLLOW,
          "a contact with nothing chosen sends from the primary account and follows the merge setting");
    CHECK(account_roster_manager_set_send_account(roster, MOM, ACCOUNT_ID_FIRST) == 0 &&
          account_roster_manager_set_merge(roster, MOM, CHAT_MERGE_NEVER) == 0 &&
          account_roster_manager_chat_prefs(roster, MOM, &p) == 0 && p.send_account == ACCOUNT_ID_FIRST && p.merge == CHAT_MERGE_NEVER,
          "a contact gets a sending account and a merge choice of its own, each kept when the other is set");
    CHECK(account_roster_manager_set_send_account(roster, FAM, 99) != 0, "a sending account that does not exist is refused");
    CHECK(account_roster_manager_set_send_account(roster, FAM, work) == 0, "a group gets a sending account too");
    ChatPrefs list[8];
    CHECK(account_roster_manager_send_accounts(roster, list, 8) == 2, "everyone with a sending account of their own is listed");
    CHECK(prefs->reassign_jid(prefs, MOM, "9999@lid") == 0 && account_roster_manager_chat_prefs(roster, "9999@lid", &p) == 0 &&
          p.send_account == ACCOUNT_ID_FIRST && p.merge == CHAT_MERGE_NEVER &&
          account_roster_manager_chat_prefs(roster, MOM, &p) == 0 && p.send_account == ACCOUNT_ID_NONE,
          "a contact's choices follow them to another address");

    /* Removing */
    CHECK(account_roster_manager_remove(roster, 99) != 0, "an account that does not exist cannot be removed");
    CHECK(account_roster_manager_remove(roster, work) == 0 && account_roster_manager_list(roster, all, ACCOUNT_MAX) == 1 &&
          account_roster_manager_primary(roster) == ACCOUNT_ID_FIRST, "removing the primary account hands that to the one left");
    CHECK(account_roster_manager_chat_prefs(roster, FAM, &p) == 0 && p.send_account == ACCOUNT_ID_NONE,
          "a contact that sent from a removed account is back on the primary one");
    CHECK(account_roster_manager_remove(roster, ACCOUNT_ID_FIRST) != 0 && account_roster_manager_error(roster)[0],
          "the last account cannot be removed");
    AccountId third = ACCOUNT_ID_NONE;
    CHECK(account_roster_manager_add(roster, "Work", NOW, &third) == 0 && third == 3, "an id is never used a second time");

    for (int i = 0; i < ACCOUNT_MAX; i++) {
        char label[32];
        snprintf(label, sizeof(label), "extra %d", i);
        account_roster_manager_add(roster, label, NOW, NULL);
    }
    CHECK(account_roster_manager_list(roster, all, ACCOUNT_MAX) == ACCOUNT_MAX &&
          scalar(db, "SELECT count(*) FROM accounts") == ACCOUNT_MAX, "there is a most that tawk holds");

    account_roster_manager_destroy(roster);
    prefs->destroy(prefs);
    accounts->destroy(accounts);
}

static void test_isolation(sqlite3 *db) {
    const AccountId A = ACCOUNT_ID_FIRST, B = 3;          /* the account test_roster added last */
    IMessageStore *ma = sqlite_message_store_create(db, A), *mb = sqlite_message_store_create(db, B);
    IChatStore *ca = sqlite_chat_store_create(db, A), *cb = sqlite_chat_store_create(db, B);
    IContactStore *na = sqlite_contact_store_create(db, A), *nb = sqlite_contact_store_create(db, B);
    IReactionStore *ra = sqlite_reaction_store_create(db, A), *rb = sqlite_reaction_store_create(db, B);
    IReceiptStore *ea = sqlite_receipt_store_create(db, A), *eb = sqlite_receipt_store_create(db, B);
    IProfileStore *pa = sqlite_profile_store_create(db, A), *pb = sqlite_profile_store_create(db, B);
    IStatusStore *sa = sqlite_status_store_create(db, A), *sb = sqlite_status_store_create(db, B);
    IScheduledMessageStore *wa = sqlite_scheduled_message_store_create(db, A), *wb = sqlite_scheduled_message_store_create(db, B);

    /* The same group message reaches both accounts with one id. */
    save_message(ma, "G1", FAM, "lunch on sunday", NOW);
    save_message(mb, "G1", FAM, "lunch on sunday", NOW);
    save_message(ma, "A1", MOM, "only on the first account", NOW + 1);
    CHECK(scalar(db, "SELECT count(*) FROM messages WHERE id = 'G1'") == 2, "one message id is kept once for each account");
    CHECK(count_recent(ma, FAM) == 1 && count_recent(mb, FAM) == 1, "each account sees the group message once");
    CHECK(count_recent(ma, MOM) == 1 && count_recent(mb, MOM) == 0, "a message of one account is not in the other");
    Message got;
    CHECK(mb->get(mb, "A1", &got) != 0, "nor can it be fetched by id from the other");
    CHECK(count_search(ma, "sunday") == 1 && count_search(mb, "sunday") == 1 && count_search(mb, "first account") == 0,
          "search stays inside the account");
    CHECK(mb->edit_text(mb, "G1", "lunch on saturday", 0) == 0 && count_search(mb, "saturday") == 1 && count_search(ma, "saturday") == 0,
          "editing a message in one account leaves the other's copy");
    CHECK(mb->remove(mb, "G1") == 0 && count_recent(mb, FAM) == 0 && count_recent(ma, FAM) == 1,
          "removing it from one account leaves it in the other");

    Chat chat;
    chat_init(&chat, MOM);
    str_copy(chat.name, sizeof(chat.name), "Mom");
    chat.last_ts = NOW;
    chat.unread = 2;
    CHECK(ca->upsert(ca, &chat) == 0, "a chat is saved for the first account");
    chat.unread = 5;
    CHECK(cb->upsert(cb, &chat) == 0, "the same contact is a chat of the second account too");
    Chat *list = NULL;
    int n = 0;
    CHECK(ca->get_all(ca, &list, &n) == 0 && n == 1 && list[0].unread == 2, "each account lists only its own chats");
    free(list);
    CHECK(cb->set_pinned(cb, MOM, 1) == 0 && ca->get(ca, MOM, &chat) == 0 && !chat.is_pinned &&
          cb->get(cb, MOM, &chat) == 0 && chat.is_pinned && chat.unread == 5, "pinning a chat in one account does not pin it in the other");
    CHECK(cb->remove(cb, MOM) == 0 && ca->get(ca, MOM, &chat) == 0, "removing a chat from one account leaves the other's");

    Contact contact;
    contact_init(&contact, MOM);
    str_copy(contact.name, sizeof(contact.name), "Mom");
    CHECK(na->upsert(na, &contact) == 0 && nb->get(nb, MOM, &contact) != 0, "a contact saved by one account is unknown to the other");

    CHECK(ra->put(ra, "A1", MOM, "x") == 0, "a reaction is saved");
    Reaction reactions[4];
    CHECK(ra->list(ra, "A1", reactions, 4) == 1 && rb->list(rb, "A1", reactions, 4) == 0, "reactions stay with their account");
    Receipt receipts[4];
    CHECK(ea->put(ea, "A1", MOM, RECEIPT_READ, NOW) == 0 && ea->list(ea, "A1", receipts, 4) == 1 && eb->list(eb, "A1", receipts, 4) == 0,
          "receipts stay with their account");

    CHECK(pa->set_blocklist(pa, MOM) == 0 && pb->set_blocklist(pb, "") == 0, "each account has its own block list");
    ContactProfile profile;
    CHECK(pa->get(pa, MOM, &profile) == 0 && profile.blocked, "clearing one account's block list leaves the other's");
    contact_profile_dispose(&profile);

    StatusUpdate status;
    memset(&status, 0, sizeof(status));
    str_copy(status.id, sizeof(status.id), "S1");
    str_copy(status.author_jid, sizeof(status.author_jid), MOM);
    status.timestamp = NOW;
    StatusAuthor authors[4];
    CHECK(sa->save(sa, &status) == 0 && sa->authors(sa, NOW - 10, NOW + 10, authors, 4) == 1 && sb->authors(sb, NOW - 10, NOW + 10, authors, 4) == 0,
          "a status is seen by the account it reached");
    CHECK(sb->prune(sb, NOW + 100, NULL, NULL) == 0 && sa->authors(sa, NOW - 10, NOW + 10, authors, 4) == 1,
          "forgetting old statuses in one account leaves the other's");

    ScheduledMessage later;
    memset(&later, 0, sizeof(later));
    str_copy(later.id, sizeof(later.id), "W1");
    str_copy(later.chat_jid, sizeof(later.chat_jid), MOM);
    later.text = str_dup("see you");
    later.due_at = NOW + 60;
    later.created_at = NOW;
    ScheduledMessage *due = NULL;
    CHECK(wa->add(wa, &later) == 0 && wa->due(wa, NOW + 120, &due, &n) == 0 && n == 1, "a message waits to be sent by its own account");
    for (int i = 0; i < n; i++) scheduled_message_dispose(&due[i]);
    free(due);
    due = NULL;
    CHECK(wb->due(wb, NOW + 120, &due, &n) == 0 && n == 0, "and is not due for another");
    free(due);
    scheduled_message_dispose(&later);

    /* Removing the first account takes all of that and leaves the second whole. */
    save_message(mb, "B1", MOM, "kept", NOW + 5);
    IAccountStore *accounts = sqlite_account_store_create(db);
    CHECK(accounts->remove(accounts, A) == 0, "an account is removed");
    static const char *const TABLES[] = { "messages", "chats", "contacts", "reactions", "message_receipts", "profiles", "statuses",
                                          "scheduled_messages" };
    for (size_t i = 0; i < sizeof(TABLES) / sizeof(TABLES[0]); i++) {
        char sql[160], what[160];
        snprintf(sql, sizeof(sql), "SELECT count(*) FROM %s WHERE account_id = %d", TABLES[i], A);
        snprintf(what, sizeof(what), "nothing of the removed account is left in %s", TABLES[i]);
        CHECK(scalar(db, sql) == 0, what);
    }
    CHECK(count_recent(mb, MOM) == 1 && count_search(mb, "kept") == 1 && count_search(mb, "first account") == 0,
          "the other account keeps its messages, and search no longer finds the removed ones");
    accounts->destroy(accounts);

    ma->destroy(ma); mb->destroy(mb);
    ca->destroy(ca); cb->destroy(cb);
    na->destroy(na); nb->destroy(nb);
    ra->destroy(ra); rb->destroy(rb);
    ea->destroy(ea); eb->destroy(eb);
    pa->destroy(pa); pb->destroy(pb);
    sa->destroy(sa); sb->destroy(sb);
    wa->destroy(wa); wb->destroy(wb);
}

static void test_aliases(sqlite3 *db) {
    IJidAliasStore *a = sqlite_jid_alias_store_create(db, 3), *b = sqlite_jid_alias_store_create(db, 4);
    CHECK(a->put(a, "1234@lid", MOM) == 1 && strcmp(a->resolve(a, "1234@lid"), MOM) == 0, "an account learns an alias");
    CHECK(strcmp(b->resolve(b, "1234@lid"), "1234@lid") == 0, "another account does not know it");
    a->destroy(a);
    b->destroy(b);
    a = sqlite_jid_alias_store_create(db, 3);
    b = sqlite_jid_alias_store_create(db, 4);
    CHECK(strcmp(a->resolve(a, "1234@lid"), MOM) == 0 && strcmp(b->resolve(b, "1234@lid"), "1234@lid") == 0,
          "and the same holds when the stores are made again");
    a->destroy(a);
    b->destroy(b);
}

int main(void) {
    char dir[] = "/tmp/tawk-accounts-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    char path[600];
    snprintf(path, sizeof(path), "%s/tawk.db", dir);
    sqlite3 *db = sqlite_database_open(path, NULL);
    CHECK(db != NULL, "the database opens");
    if (db) {
        test_labels();
        test_roster(db);
        test_isolation(db);
        test_aliases(db);
        sqlite_database_close(db);
    }
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures == 0) printf("ok: accounts keep their own rows in one database, are named, ordered and removed, and a contact's choices span them\n");
    return failures != 0;
}
