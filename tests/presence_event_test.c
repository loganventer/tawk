/* A contact coming online or leaving, from the backend's line to the chat
 * row: what the line decodes to, what the manager shows, what it tells those
 * who follow along, and when it forgets. */
#include "core/event.h"
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

#define MOM    "27820000001@s.whatsapp.net"
#define FAMILY "120363000000000001@g.us"

static void test_decode(void) {
    Event e;
    CHECK(json_protocol_decode("{\"evt\":\"presence\",\"jid\":\"" MOM "\",\"state\":\"online\",\"last_seen\":0}", &e) == 0 &&
          e.type == EVENT_PRESENCE && strcmp(e.jid, MOM) == 0 && strcmp(e.chat.jid, MOM) == 0 && strcmp(e.state, "online") == 0 && e.at == 0,
          "the backend's line says who came online");
    event_dispose(&e);
    CHECK(json_protocol_decode("{\"evt\":\"presence\",\"jid\":\"" MOM "\",\"state\":\"offline\",\"last_seen\":1791363900}", &e) == 0 &&
          e.type == EVENT_PRESENCE && strcmp(e.state, "offline") == 0 && e.at == 1791363900, "and when someone who left was last seen");
    event_dispose(&e);
    CHECK(json_protocol_decode("{\"evt\":\"presence\",\"state\":\"online\"}", &e) != 0, "a line about nobody is dropped");
    CHECK(json_protocol_decode("{\"evt\":\"presence\",\"jid\":\"" MOM "\"}", &e) != 0, "and so is one that says nothing");
}

/* ---- the messaging manager with a fake backend ---------------------------- */

static int shown_online = -1;

static int fake_start(IMessageGateway *self) { (void)self; return 0; }
static int fake_connect(IMessageGateway *self) { (void)self; return 0; }
static int fake_presence(IMessageGateway *self, int available) { (void)self; shown_online = available; return 0; }
static int fake_subscribe(IMessageGateway *self, const char *jid) { (void)self; (void)jid; return 0; }
static int fake_typing(IMessageGateway *self, const char *jid, const char *state) { (void)self; (void)jid; (void)state; return 0; }
static void fake_notify(INotifier *self, const Notification *n) { (void)self; (void)n; }

static void push_connection(EventQueue *q, const char *reason) {
    Event e;
    event_init(&e, EVENT_CONNECTION_STATUS);
    str_copy(e.reason, sizeof(e.reason), reason);
    event_queue_push(q, &e);
}

static void push_message(EventQueue *q, const char *id, const char *chat) {
    Event e;
    event_init(&e, EVENT_MESSAGE_UPSERT);
    e.live = 1;
    str_copy(e.message.id, sizeof(e.message.id), id);
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), chat);
    str_copy(e.message.sender_jid, sizeof(e.message.sender_jid), MOM);
    e.message.timestamp = 1791363000;
    message_set_text(&e.message, "hello");
    event_queue_push(q, &e);
}

static void push_presence(EventQueue *q, const char *jid, const char *state, int64_t last_seen) {
    Event e;
    event_init(&e, EVENT_PRESENCE);
    str_copy(e.jid, sizeof(e.jid), jid);
    str_copy(e.chat.jid, sizeof(e.chat.jid), jid);
    str_copy(e.state, sizeof(e.state), state);
    e.at = last_seen;
    event_queue_push(q, &e);
}

static const Chat *chat_of(MessagingManager *m, const char *jid) {
    int n = 0;
    const Chat *all = messaging_manager_chats(m, &n);
    for (int i = 0; i < n; i++) {
        if (strcmp(all[i].jid, jid) == 0) return &all[i];
    }
    return NULL;
}

/* The presence notes written since `after`, and the last of them. */
static int presence_notes(MessagingManager *m, uint64_t after, LiveMessageRef *last) {
    LiveMessageRef refs[32];
    int n = messaging_manager_live_since(m, after, refs, 32), found = 0;
    for (int i = 0; i < n; i++) {
        if (refs[i].kind != LIVE_KIND_PRESENCE) continue;
        found++;
        if (last) *last = refs[i];
    }
    return found;
}

static void test_manager(void) {
    char path[] = "/tmp/tawk-presence-test-XXXXXX";
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
    gateway.presence = fake_presence;
    gateway.subscribe = fake_subscribe;
    gateway.typing = fake_typing;
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
                                  &settings, NULL, exporter, NULL, NULL, ACCOUNT_ID_FIRST, NULL, NULL, NULL, NULL };
    MessagingManager *m = messaging_manager_create(&deps);
    ManagerChanges ch;

    messaging_manager_start(m);
    messaging_manager_set_active(m, 1);
    push_connection(events, "open");
    push_message(events, "P1", MOM);
    push_message(events, "P2", FAMILY);
    messaging_manager_tick(m, &ch);
    CHECK(shown_online == 1, "connected and in use, we show as online ourselves");
    const Chat *mom = chat_of(m, MOM);
    CHECK(mom && mom->presence == PRESENCE_UNKNOWN && mom->last_seen == 0, "nothing is known about her yet");

    uint64_t mark = messaging_manager_live_last(m);
    push_presence(events, MOM, "online", 0);
    messaging_manager_tick(m, &ch);
    CHECK(ch.chats, "her coming online redraws the chats");
    mom = chat_of(m, MOM);
    CHECK(mom && mom->presence == PRESENCE_ONLINE, "and her chat says she is online");
    LiveMessageRef note;
    memset(&note, 0, sizeof(note));
    CHECK(presence_notes(m, mark, &note) == 1 && strcmp(note.chat_jid, MOM) == 0 && strcmp(note.who, MOM) == 0 &&
          strcmp(note.detail, "online") == 0 && note.id[0] == '\0', "those who follow along hear it once, with no message id");

    mark = messaging_manager_live_last(m);
    push_presence(events, MOM, "online", 0);
    messaging_manager_tick(m, &ch);
    CHECK(presence_notes(m, mark, NULL) == 0, "hearing it again is not news");

    push_presence(events, MOM, "offline", 1791363900);
    messaging_manager_tick(m, &ch);
    mom = chat_of(m, MOM);
    CHECK(mom && mom->presence == PRESENCE_OFFLINE && mom->last_seen == 1791363900, "when she leaves the chat says when she was last seen");
    CHECK(presence_notes(m, mark, &note) == 1 && strcmp(note.detail, "offline") == 0 && note.at == 1791363900,
          "and that is news, with the time");

    mark = messaging_manager_live_last(m);
    push_presence(events, FAMILY, "online", 0);
    messaging_manager_tick(m, &ch);
    const Chat *family = chat_of(m, FAMILY);
    CHECK(family && family->presence == PRESENCE_UNKNOWN && presence_notes(m, mark, NULL) == 0, "a group is never online");

    push_presence(events, MOM, "online", 0);
    messaging_manager_tick(m, &ch);
    CHECK(chat_of(m, MOM)->presence == PRESENCE_ONLINE, "she is back");
    messaging_manager_set_active(m, 0);
    messaging_manager_tick(m, &ch);
    CHECK(shown_online == 0, "when tawk goes idle we show as offline");
    mom = chat_of(m, MOM);
    CHECK(mom && mom->presence == PRESENCE_UNKNOWN && mom->last_seen == 0, "and what we knew about her no longer holds, so it is gone");
    messaging_manager_set_active(m, 1);
    messaging_manager_tick(m, &ch);
    CHECK(chat_of(m, MOM)->presence == PRESENCE_UNKNOWN, "coming back does not bring the old state with it");

    push_presence(events, MOM, "online", 0);
    messaging_manager_tick(m, &ch);
    CHECK(chat_of(m, MOM)->presence == PRESENCE_ONLINE, "until WhatsApp says so again");
    push_connection(events, "closed");
    messaging_manager_tick(m, &ch);
    CHECK(chat_of(m, MOM)->presence == PRESENCE_UNKNOWN, "a dropped connection forgets it too");

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
    test_decode();
    test_manager();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: a contact coming online or leaving shows on their chat, is told once, and is forgotten when we go offline\n");
    return 0;
}
