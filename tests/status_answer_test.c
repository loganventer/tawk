/* Answering other people's statuses: a reply travels as a message quoting
 * the status, a like goes privately where the backend can and as a ❤️ reply
 * where it cannot, and the status mark survives the protocol and the store. */
#include "core/settings.h"
#include "managers/messaging_manager.h"
#include "resource_access/json_protocol.h"
#include "resource_access/sqlite_chat_store.h"
#include "resource_access/sqlite_contact_store.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_jid_alias_store.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_receipt_store.h"
#include "resource_access/text_chat_exporter.h"
#include "utilities/event_queue.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static int texts, likes, last_is_status;
static char last_jid[128], last_text[64], last_quote[64], last_emoji[16], last_id[64];

static int fake_send_text(IMessageGateway *self, const char *jid, const OutgoingText *t, const char *id) {
    (void)self;
    texts++;
    str_copy(last_jid, sizeof(last_jid), jid);
    str_copy(last_text, sizeof(last_text), t->text);
    str_copy(last_quote, sizeof(last_quote), t->quote ? t->quote->id : "");
    str_copy(last_id, sizeof(last_id), id);
    last_is_status = t->quote && t->quote->is_status;
    return 0;
}

static int fake_like(IStatusLiker *self, const char *author, const char *status_id, const char *emoji) {
    (void)self;
    likes++;
    str_copy(last_jid, sizeof(last_jid), author);
    str_copy(last_quote, sizeof(last_quote), status_id);
    str_copy(last_emoji, sizeof(last_emoji), emoji);
    return 0;
}

static int fake_ok(IMessageGateway *self) { (void)self; return 0; }
static void fake_notify(INotifier *self, const Notification *n) { (void)self; (void)n; }

static void test_protocol(void) {
    QuoteRef quote;
    memset(&quote, 0, sizeof(quote));
    str_copy(quote.id, sizeof(quote.id), "S1");
    str_copy(quote.sender, sizeof(quote.sender), "a@s.whatsapp.net");
    quote.is_status = 1;
    OutgoingText t = { "lovely", &quote, NULL, 0, 0, 0 };
    char *json = json_protocol_encode_send("a@s.whatsapp.net", &t, "ID");
    CHECK(json && strstr(json, "\"status\":true"), "a reply to a status says so");
    free(json);
    quote.is_status = 0;
    json = json_protocol_encode_send("a@s.whatsapp.net", &t, "ID");
    CHECK(json && !strstr(json, "\"status\""), "an ordinary reply does not");
    free(json);

    json = json_protocol_encode_like_status("a@s.whatsapp.net", "S1", "\xE2\x9D\xA4\xEF\xB8\x8F");
    CHECK(json && strstr(json, "\"cmd\":\"like_status\"") && strstr(json, "\"id\":\"S1\""), "a like names the status");
    free(json);

    Event e;
    CHECK(json_protocol_decode("{\"evt\":\"message\",\"id\":\"M1\",\"chat\":\"a@s.whatsapp.net\",\"sender\":\"a@s.whatsapp.net\","
                               "\"type\":\"text\",\"text\":\"nice\",\"quote\":{\"id\":\"S9\",\"sender\":\"me@s.whatsapp.net\","
                               "\"text\":\"my status\",\"status\":true}}", &e) == 0 && e.message.quoted_status &&
          strcmp(e.message.quoted_id, "S9") == 0, "a reply to your status is read as one");
    event_dispose(&e);
}

static MessagingManager *manager(IMessageGateway *gw, IStatusLiker *liker, sqlite3 *db, Settings *settings,
                                 INotifier *notifier, EventQueue *events, void *stores[7]) {
    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    IChatStore *chats = sqlite_chat_store_create(db, ACCOUNT_ID_FIRST);
    IContactStore *contacts = sqlite_contact_store_create(db, ACCOUNT_ID_FIRST);
    IJidAliasStore *aliases = sqlite_jid_alias_store_create(db, ACCOUNT_ID_FIRST);
    IReactionStore *reactions = sqlite_reaction_store_create(db, ACCOUNT_ID_FIRST);
    IReceiptStore *receipts = sqlite_receipt_store_create(db, ACCOUNT_ID_FIRST);
    IChatExporter *exporter = text_chat_exporter_create();
    void *all[7] = { messages, chats, contacts, aliases, reactions, receipts, exporter };
    memcpy(stores, all, sizeof(all));
    MessagingManagerDeps deps = { gw, messages, chats, contacts, aliases, reactions, receipts, notifier, events,
                                  settings, NULL, exporter, NULL, liker, ACCOUNT_ID_FIRST, NULL, NULL, NULL, NULL };
    return messaging_manager_create(&deps);
}

static void release(MessagingManager *m, void *stores[7]) {
    messaging_manager_destroy(m);
    ((IChatExporter *)stores[6])->destroy(stores[6]);
    ((IReceiptStore *)stores[5])->destroy(stores[5]);
    ((IReactionStore *)stores[4])->destroy(stores[4]);
    ((IJidAliasStore *)stores[3])->destroy(stores[3]);
    ((IContactStore *)stores[2])->destroy(stores[2]);
    ((IChatStore *)stores[1])->destroy(stores[1]);
    ((IMessageStore *)stores[0])->destroy(stores[0]);
}

static void test_manager(const char *dir) {
    char db_path[600];
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", dir);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) { failures++; return; }
    Settings settings;
    settings_set_defaults(&settings);
    str_copy(settings.media_dir, sizeof(settings.media_dir), dir);
    IMessageGateway gw;
    memset(&gw, 0, sizeof(gw));
    gw.start = fake_ok;
    gw.connect = fake_ok;
    gw.send_text = fake_send_text;
    INotifier notifier = { NULL, fake_notify, NULL };
    EventQueue *events = event_queue_create(16);
    StatusReplyTarget target;
    memset(&target, 0, sizeof(target));
    str_copy(target.status_id, sizeof(target.status_id), "S1");
    str_copy(target.author_jid, sizeof(target.author_jid), "27820000001@s.whatsapp.net");
    str_copy(target.preview, sizeof(target.preview), "At the beach");

    void *stores[7];
    MessagingManager *m = manager(&gw, NULL, db, &settings, &notifier, events, stores);
    CHECK(messaging_manager_reply_to_status(m, &target, "Looks warm") == 0 && texts == 1 && last_is_status &&
          strcmp(last_jid, target.author_jid) == 0 && strcmp(last_quote, "S1") == 0,
          "a reply goes to the author's chat, quoting the status");
    IMessageStore *messages = stores[0];
    Message kept;
    CHECK(messages->get(messages, last_id, &kept) == 0 && kept.quoted_status && strcmp(kept.quoted_id, "S1") == 0,
          "and is kept as a reply to a status");
    message_dispose(&kept);
    CHECK(messaging_manager_like_status(m, &target) == 1 && texts == 2 && last_is_status &&
          strcmp(last_text, "\xE2\x9D\xA4\xEF\xB8\x8F") == 0, "without a private like, a like is a heart in the chat");
    CHECK(messaging_manager_reply_to_status(m, &target, "") == -1 && texts == 2, "an empty reply is not sent");
    release(m, stores);

    IStatusLiker liker = { NULL, fake_like };
    m = manager(&gw, &liker, db, &settings, &notifier, events, stores);
    CHECK(messaging_manager_like_status(m, &target) == 0 && likes == 1 && texts == 2 && strcmp(last_quote, "S1") == 0,
          "with one, the like goes privately to the author");
    release(m, stores);

    event_queue_close(events);
    event_queue_destroy(events);
    sqlite_database_close(db);
}

int main(void) {
    char dir[] = "/tmp/tawk-status-answer-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    test_protocol();
    test_manager(dir);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures == 0) printf("ok: statuses are answered with a reply, an emoji or a like, and replies to them are marked\n");
    return failures != 0;
}
