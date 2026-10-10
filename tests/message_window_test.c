/* The conversation keeps a window of messages in memory: a margin either side
 * of the ones on screen. It slides as the user scrolls, so memory stays flat,
 * and at the newest message only the ones before it are kept. */
#include "core/settings.h"
#include "managers/messaging_manager.h"
#include "resource_access/caching_message_store.h"
#include "resource_access/sqlite_chat_store.h"
#include "resource_access/sqlite_contact_store.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_jid_alias_store.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_receipt_store.h"
#include "utilities/event_queue.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHAT  "27820000001@s.whatsapp.net"
#define TOTAL 400

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static int fake_ok(IMessageGateway *self) { (void)self; return 0; }
static void fake_notify(INotifier *self, const Notification *n) { (void)self; (void)n; }

/* Message number n of the chat, 1 the oldest. Several share a second, as they do after a history sync. */
static void store(IMessageStore *s, int n) {
    Message m;
    char text[32];
    message_init(&m);
    snprintf(m.id, sizeof(m.id), "M%03d", n);
    str_copy(m.chat_jid, sizeof(m.chat_jid), CHAT);
    str_copy(m.sender_jid, sizeof(m.sender_jid), CHAT);
    m.type = MESSAGE_TYPE_TEXT;
    m.timestamp = 1790000000 + n / 3;
    snprintf(text, sizeof(text), "message %d", n);
    message_set_text(&m, text);
    s->save(s, &m);
    message_dispose(&m);
}

static int number(const Message *m) { return atoi(m->id + 1); }

/* The loaded messages are one unbroken run, oldest first. */
static int unbroken(const Message *msgs, int count) {
    for (int i = 1; i < count; i++) if (number(&msgs[i]) != number(&msgs[i - 1]) + 1) return 0;
    return 1;
}

static void test_slice(IMessageStore *messages) {
    Message *page = NULL;
    int n = 0;
    CHECK(messages->slice(messages, CHAT, 0, 10, &page, &n) == 0 && n == 10 && number(&page[0]) == TOTAL - 9 &&
          number(&page[9]) == TOTAL, "a slice from the newest is the newest messages, oldest first");
    message_array_free(page, n);
    CHECK(messages->slice(messages, CHAT, 100, 50, &page, &n) == 0 && n == 50 && number(&page[49]) == TOTAL - 100 &&
          number(&page[0]) == TOTAL - 149 && unbroken(page, n), "a slice further back leaves out exactly the newest ones asked");
    message_array_free(page, n);
    CHECK(messages->slice(messages, CHAT, TOTAL - 5, 50, &page, &n) == 0 && n == 5 && number(&page[0]) == 1,
          "a slice at the start of the chat holds what is left");
    message_array_free(page, n);
    CHECK(messages->slice(messages, CHAT, TOTAL + 10, 50, &page, &n) == 0 && n == 0, "a slice past the start is empty");
    message_array_free(page, n);
}

static void test_window(MessagingManager *m, EventQueue *events) {
    int count = 0;
    messaging_manager_open_chat(m, CHAT);
    const Message *msgs = messaging_manager_messages(m, &count);
    CHECK(count == 75 && number(&msgs[count - 1]) == TOTAL, "a chat opens on its newest messages, a margin and a half of them");
    CHECK(!messaging_manager_has_newer(m), "nothing newer is missing at the newest message");

    /* At the newest message with 10 on screen: only the ones before are kept, and nothing reloads. */
    CHECK(!messaging_manager_focus_window(m, count - 10, count - 1), "the newest messages on screen need no reload");

    /* Scroll up until few are left above the screen: older ones come in. */
    CHECK(messaging_manager_focus_window(m, 10, 19), "running short above loads older messages");
    msgs = messaging_manager_messages(m, &count);
    int top = -1;
    for (int i = 0; i < count; i++) if (number(&msgs[i]) == TOTAL - 64) top = i;
    CHECK(top == 50 && unbroken(msgs, count), "the message that was at the top of the screen now has a margin above it");
    CHECK(count == 50 + 10 + 50 && number(&msgs[count - 1]) == TOTAL - 5 && messaging_manager_has_newer(m),
          "and the surplus below the screen is let go");
    CHECK(!messaging_manager_focus_window(m, 50, 59), "the window is then left alone");
    CHECK(!messaging_manager_focus_window(m, 40, 49), "a scroll of a few messages does not reload");

    /* Keep scrolling up, a screen at a time: memory stays flat. */
    int most = 0;
    for (int step = 0; step < 60; step++) {
        msgs = messaging_manager_messages(m, &count);
        int first = 0;
        while (first < count && number(&msgs[first]) < TOTAL - 70 - step * 5) first++;
        if (first + 9 >= count) break;
        messaging_manager_focus_window(m, first, first + 9);
        messaging_manager_messages(m, &count);
        if (count > most) most = count;
    }
    msgs = messaging_manager_messages(m, &count);
    CHECK(most <= 50 + 10 + 50 + 25 && unbroken(msgs, count), "however far back, no more than the screen and its margins (with slack) is held");
    CHECK(number(&msgs[count - 1]) < TOTAL - 200, "and the newest messages are no longer in memory");

    /* A message arrives while scrolled back: the window stays put. */
    int oldest = number(&msgs[0]), newest = number(&msgs[count - 1]);
    Event e;
    event_init(&e, EVENT_MESSAGE_UPSERT);
    e.live = 1;
    str_copy(e.message.id, sizeof(e.message.id), "M401");
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), CHAT);
    str_copy(e.message.sender_jid, sizeof(e.message.sender_jid), CHAT);
    e.message.type = MESSAGE_TYPE_TEXT;
    e.message.timestamp = 1790000999;
    message_set_text(&e.message, "a new one");
    event_queue_push(events, &e);
    ManagerChanges ch;
    memset(&ch, 0, sizeof(ch));
    messaging_manager_tick(m, &ch);
    msgs = messaging_manager_messages(m, &count);
    CHECK(number(&msgs[0]) == oldest && number(&msgs[count - 1]) == newest, "a new message does not move a window that is scrolled back");

    /* Back to the newest. */
    CHECK(messaging_manager_show_latest(m), "going to the newest reloads");
    msgs = messaging_manager_messages(m, &count);
    CHECK(count == 75 && number(&msgs[count - 1]) == 401 && !messaging_manager_has_newer(m), "and shows the newest message again");
    CHECK(!messaging_manager_show_latest(m), "already there: nothing to do");

    /* The start of the chat: what is left, and no endless reloading. */
    for (int step = 0; step < 40; step++) {
        messaging_manager_messages(m, &count);
        if (!messaging_manager_focus_window(m, 0, count < 10 ? count - 1 : 9)) break;
    }
    msgs = messaging_manager_messages(m, &count);
    CHECK(number(&msgs[0]) == 1 && count <= 10 + 50 + 25, "at the oldest message only the ones after it are kept");
    CHECK(!messaging_manager_focus_window(m, 0, 9), "and the window settles");
}

int main(void) {
    char dir[] = "/tmp/tawk-window-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char db_path[600];
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", dir);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) return 1;
    Settings settings;
    settings_set_defaults(&settings);
    IMessageGateway gw;
    memset(&gw, 0, sizeof(gw));
    gw.start = fake_ok;
    gw.connect = fake_ok;
    INotifier notifier = { NULL, fake_notify, NULL };
    EventQueue *events = event_queue_create(16);
    IMessageStore *messages = caching_message_store_create(sqlite_message_store_create(db, ACCOUNT_ID_FIRST), 4);
    IChatStore *chats = sqlite_chat_store_create(db, ACCOUNT_ID_FIRST);
    IContactStore *contacts = sqlite_contact_store_create(db, ACCOUNT_ID_FIRST);
    IJidAliasStore *aliases = sqlite_jid_alias_store_create(db, ACCOUNT_ID_FIRST);
    IReactionStore *reactions = sqlite_reaction_store_create(db, ACCOUNT_ID_FIRST);
    IReceiptStore *receipts = sqlite_receipt_store_create(db, ACCOUNT_ID_FIRST);
    MessagingManagerDeps deps = { &gw, messages, chats, contacts, aliases, reactions, receipts, &notifier, events,
                                  &settings, NULL, NULL, NULL, NULL, ACCOUNT_ID_FIRST, NULL, NULL, NULL, NULL };
    MessagingManager *m = messaging_manager_create(&deps);
    for (int n = 1; n <= TOTAL; n++) store(messages, n);

    CHECK(settings.message_margin == 50, "the margin is 50 messages by default");
    test_slice(messages);
    test_window(m, events);

    messaging_manager_destroy(m);
    event_queue_close(events);
    event_queue_destroy(events);
    receipts->destroy(receipts);
    reactions->destroy(reactions);
    aliases->destroy(aliases);
    contacts->destroy(contacts);
    chats->destroy(chats);
    messages->destroy(messages);
    sqlite_database_close(db);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) failures++;
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: the conversation keeps a margin of messages either side of the screen, and slides as you scroll\n");
    return 0;
}
