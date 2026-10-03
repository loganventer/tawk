/* Getting the connection back after an outage: the circuit breaker, network
 * change detection and the messaging manager's reconnect on a new network. */
#include "core/event.h"
#include "core/settings.h"
#include "engines/circuit_breaker.h"
#include "engines/network_change_detector.h"
#include "engines/network_fingerprint.h"
#include "managers/messaging_manager.h"
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

static void test_breaker_keeps_its_cooldown(void) {
    CircuitBreaker b;
    circuit_breaker_init(&b, 2, 10);
    circuit_breaker_record_failure(&b, 0);
    circuit_breaker_record_failure(&b, 1000);
    CHECK(b.state == CIRCUIT_OPEN, "the breaker opens at its threshold");
    for (int64_t t = 2000; t < 10000; t += 1000) circuit_breaker_record_failure(&b, t);
    CHECK(circuit_breaker_remaining_ms(&b, 5000) == 6000, "failures while open do not restart the cooldown");
    CHECK(circuit_breaker_allow(&b, 11000) && b.state == CIRCUIT_HALF_OPEN, "the breaker tries again after the cooldown");
    circuit_breaker_record_failure(&b, 11500);
    CHECK(b.state == CIRCUIT_OPEN && circuit_breaker_remaining_ms(&b, 11500) == 10000, "a failed trial opens it again");
}

static void test_fingerprint(void) {
    const char *a[] = { "eth0 10.0.0.2", "wlan0 192.168.1.5" };
    const char *b[] = { "wlan0 192.168.1.5", "eth0 10.0.0.2" };
    const char *c[] = { "eth0 10.0.0.2" };
    const char *d[] = { "eth0 10.0.0.3", "wlan0 192.168.1.5" };
    CHECK(network_fingerprint_of(a, 2) == network_fingerprint_of(b, 2), "the order of addresses does not matter");
    CHECK(network_fingerprint_of(a, 2) != network_fingerprint_of(c, 1), "losing an adapter changes the fingerprint");
    CHECK(network_fingerprint_of(a, 2) != network_fingerprint_of(d, 2), "a new address changes the fingerprint");
    CHECK(network_fingerprint_of(NULL, 0) == 0, "no addresses give 0");
}

static void test_change_detector(void) {
    NetworkChangeDetector d;
    network_change_detector_init(&d, 2000);
    CHECK(!network_change_detector_observe(&d, 11, 0), "the first sample is the baseline");
    CHECK(!network_change_detector_observe(&d, 11, 3000), "the same network is no change");
    CHECK(!network_change_detector_observe(&d, 0, 6000), "a change is not reported before it settles");
    CHECK(!network_change_detector_observe(&d, 22, 7000), "a second change restarts the wait");
    CHECK(network_change_detector_observe(&d, 22, 9000), "a settled change is reported");
    CHECK(!network_change_detector_observe(&d, 22, 12000), "it is reported once");
    CHECK(!network_change_detector_observe(&d, 11, 13000) && !network_change_detector_observe(&d, 22, 14000) &&
          !network_change_detector_observe(&d, 22, 20000), "a blip that returns to the same network is no change");
}

/* ---- the messaging manager with a fake backend and network ---------------- */

static int connects, reconnects, network_changes;

static int fake_start(IMessageGateway *self) { (void)self; return 0; }
static int fake_connect(IMessageGateway *self) { (void)self; connects++; return 0; }
static int fake_reconnect(IMessageGateway *self) { (void)self; reconnects++; return 0; }
static int fake_presence(IMessageGateway *self, int available) { (void)self; (void)available; return 0; }
static void fake_notify(INotifier *self, const Notification *n) { (void)self; (void)n; }

static int fake_changed(INetworkMonitor *self, int64_t now_ms) {
    (void)self; (void)now_ms;
    if (network_changes <= 0) return 0;
    network_changes--;
    return 1;
}

static void push_connection(EventQueue *q, const char *reason) {
    Event e;
    event_init(&e, EVENT_CONNECTION_STATUS);
    str_copy(e.reason, sizeof(e.reason), reason);
    event_queue_push(q, &e);
}

static void test_reconnects_on_network_change(void) {
    char path[] = "/tmp/tawk-recovery-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); failures++; return; }
    close(fd);
    unlink(path);
    sqlite3 *db = sqlite_database_open(path, NULL);
    CHECK(db != NULL, "the database opens");
    if (!db) return;

    Settings settings;
    settings_set_defaults(&settings);
    IMessageGateway gateway;
    memset(&gateway, 0, sizeof(gateway));
    gateway.start = fake_start;
    gateway.connect = fake_connect;
    gateway.reconnect = fake_reconnect;
    gateway.presence = fake_presence;
    INotifier notifier = { NULL, fake_notify, NULL };
    INetworkMonitor network = { NULL, fake_changed, NULL };
    EventQueue *events = event_queue_create(64);
    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    IChatStore *chats = sqlite_chat_store_create(db, ACCOUNT_ID_FIRST);
    IContactStore *contacts = sqlite_contact_store_create(db, ACCOUNT_ID_FIRST);
    IJidAliasStore *aliases = sqlite_jid_alias_store_create(db, ACCOUNT_ID_FIRST);
    IReactionStore *reactions = sqlite_reaction_store_create(db, ACCOUNT_ID_FIRST);
    IReceiptStore *receipts = sqlite_receipt_store_create(db, ACCOUNT_ID_FIRST);
    IChatExporter *exporter = text_chat_exporter_create();
    MessagingManagerDeps deps = { &gateway, messages, chats, contacts, aliases, reactions, receipts, &notifier, events,
                                  &settings, NULL, exporter, &network, NULL };
    MessagingManager *m = messaging_manager_create(&deps);
    ManagerChanges ch;

    messaging_manager_start(m);
    CHECK(connects == 1, "starting connects");
    push_connection(events, "open");
    messaging_manager_tick(m, &ch);
    CHECK(messaging_manager_auth_state(m) == AUTH_STATE_CONNECTED, "an open connection is connected");

    network_changes = 1;
    messaging_manager_tick(m, &ch);
    CHECK(reconnects == 1, "a network change reconnects at once");
    CHECK(messaging_manager_auth_state(m) == AUTH_STATE_RECONNECTING, "and shows that it is reconnecting");
    messaging_manager_tick(m, &ch);
    CHECK(reconnects == 1, "only once");
    push_connection(events, "open");
    messaging_manager_tick(m, &ch);
    CHECK(messaging_manager_auth_state(m) == AUTH_STATE_CONNECTED, "the new connection is connected");

    push_connection(events, "closed");
    messaging_manager_tick(m, &ch);
    messaging_manager_retry_now(m);
    messaging_manager_tick(m, &ch);
    CHECK(reconnects == 2, "retrying by hand drops the old connection rather than trusting it");

    messaging_manager_destroy(m);
    event_queue_close(events);
    event_queue_destroy(events);
    exporter->destroy(exporter);
    receipts->destroy(receipts);
    reactions->destroy(reactions);
    aliases->destroy(aliases);
    contacts->destroy(contacts);
    chats->destroy(chats);
    messages->destroy(messages);
    sqlite_database_close(db);
    unlink(path);
}

/* ---- marking chats read ---------------------------------------------------- */

static ReadRequest last_read;
static int reads;

static int fake_mark_read(IMessageGateway *self, const ReadRequest *r) { (void)self; last_read = *r; reads++; return 0; }
static int fake_subscribe(IMessageGateway *self, const char *jid) { (void)self; (void)jid; return 0; }

static void store_incoming(IMessageStore *store, const char *id, int64_t ts, int from_me) {
    Message msg;
    message_init(&msg);
    str_copy(msg.id, sizeof(msg.id), id);
    str_copy(msg.chat_jid, sizeof(msg.chat_jid), "27820000001@s.whatsapp.net");
    str_copy(msg.sender_jid, sizeof(msg.sender_jid), from_me ? "me@s.whatsapp.net" : "27820000001@s.whatsapp.net");
    msg.type = MESSAGE_TYPE_TEXT;
    msg.from_me = from_me;
    msg.timestamp = ts;
    message_set_text(&msg, "hello");
    store->save(store, &msg);
    message_dispose(&msg);
}

static void test_marks_chats_read(void) {
    char path[] = "/tmp/tawk-read-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); failures++; return; }
    close(fd);
    unlink(path);
    sqlite3 *db = sqlite_database_open(path, NULL);
    if (!db) { failures++; return; }

    Settings settings;
    settings_set_defaults(&settings);
    settings.send_read_receipts = 0;
    IMessageGateway gateway;
    memset(&gateway, 0, sizeof(gateway));
    gateway.start = fake_start;
    gateway.connect = fake_connect;
    gateway.reconnect = fake_reconnect;
    gateway.presence = fake_presence;
    gateway.subscribe = fake_subscribe;
    gateway.mark_read = fake_mark_read;
    INotifier notifier = { NULL, fake_notify, NULL };
    EventQueue *events = event_queue_create(64);
    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    IChatStore *chats = sqlite_chat_store_create(db, ACCOUNT_ID_FIRST);
    IContactStore *contacts = sqlite_contact_store_create(db, ACCOUNT_ID_FIRST);
    IJidAliasStore *aliases = sqlite_jid_alias_store_create(db, ACCOUNT_ID_FIRST);
    IReactionStore *reactions = sqlite_reaction_store_create(db, ACCOUNT_ID_FIRST);
    IReceiptStore *receipts = sqlite_receipt_store_create(db, ACCOUNT_ID_FIRST);
    IChatExporter *exporter = text_chat_exporter_create();
    MessagingManagerDeps deps = { &gateway, messages, chats, contacts, aliases, reactions, receipts, &notifier, events,
                                  &settings, NULL, exporter, NULL, NULL };

    /* Two unread messages from before tawk started, and one of yours after them. */
    Chat chat;
    chat_init(&chat, "27820000001@s.whatsapp.net");
    str_copy(chat.name, sizeof(chat.name), "Mom");
    chats->upsert(chats, &chat);
    store_incoming(messages, "M1", 1790000000, 0);
    store_incoming(messages, "M2", 1790000060, 0);
    store_incoming(messages, "M3", 1790000120, 0);
    store_incoming(messages, "M4", 1790000180, 1);
    chats->set_unread(chats, "27820000001@s.whatsapp.net", 2);

    MessagingManager *m = messaging_manager_create(&deps);
    ManagerChanges ch;
    messaging_manager_start(m);

    messaging_manager_open_chat(m, "27820000001@s.whatsapp.net");
    CHECK(reads == 0, "reading while offline waits for the connection");
    push_connection(events, "open");
    messaging_manager_tick(m, &ch);
    CHECK(reads == 1, "and marks the chat read once connected");
    CHECK(last_read.count == 2 && strcmp(last_read.items[0].id, "M3") == 0 && strcmp(last_read.items[1].id, "M2") == 0,
          "the unread messages come from the database, newest first");
    CHECK(strcmp(last_read.last_id, "M4") == 0 && last_read.last_from_me && last_read.last_timestamp == 1790000180,
          "the read mark is anchored at the newest message");
    CHECK(!last_read.send_receipts, "with receipts off only the read mark is sent (it clears the phone's badge)");

    messaging_manager_open_chat(m, "27820000001@s.whatsapp.net");
    CHECK(reads == 1, "a chat with nothing unread is not marked again");

    settings.send_read_receipts = 1;
    chats->set_unread(chats, "27820000001@s.whatsapp.net", 1);
    messaging_manager_open_chat(m, "27820000001@s.whatsapp.net");
    CHECK(reads == 2 && last_read.send_receipts && last_read.count == 1, "with receipts on they go too");

    messaging_manager_destroy(m);
    event_queue_close(events);
    event_queue_destroy(events);
    exporter->destroy(exporter);
    receipts->destroy(receipts);
    reactions->destroy(reactions);
    aliases->destroy(aliases);
    contacts->destroy(contacts);
    chats->destroy(chats);
    messages->destroy(messages);
    sqlite_database_close(db);
    unlink(path);
}

int main(void) {
    test_breaker_keeps_its_cooldown();
    test_fingerprint();
    test_change_detector();
    test_reconnects_on_network_change();
    test_marks_chats_read();
    if (failures == 0) printf("connection_recovery_test: all passed\n");
    return failures != 0;
}
