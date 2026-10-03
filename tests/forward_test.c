/* Forwarding: which way each kind of message travels, that every copy is
 * its own message marked as forwarded, the chat matching rules, and the
 * chat picker. */
#include "clients/tui/chat_picker.h"
#include "clients/tui/tui_palette.h"
#include "core/settings.h"
#include "engines/chat_match.h"
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

#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

/* ---- a gateway that records what was sent ---------------------------------- */

static int texts, medias, refs;
static char last_id[4][64];
static int last_forwarded, last_score;
static char last_path[600], last_ref[64];

static void remember_id(const char *id) {
    for (int i = 3; i > 0; i--) str_copy(last_id[i], sizeof(last_id[i]), last_id[i - 1]);
    str_copy(last_id[0], sizeof(last_id[0]), id);
}

static int fake_send_text(IMessageGateway *self, const char *jid, const OutgoingText *t, const char *id) {
    (void)self; (void)jid;
    texts++;
    last_forwarded = t->forwarded;
    last_score = t->forwarding_score;
    remember_id(id);
    return 0;
}

static int fake_send_media(IMessageGateway *self, const char *jid, const OutgoingMedia *m, const char *id) {
    (void)self; (void)jid;
    medias++;
    last_forwarded = m->forwarded;
    str_copy(last_path, sizeof(last_path), m->path);
    remember_id(id);
    return 0;
}

static int fake_forward_media(IMessageGateway *self, const char *jid, const char *ref, int score, const char *id) {
    (void)self; (void)jid;
    refs++;
    last_score = score;
    str_copy(last_ref, sizeof(last_ref), ref);
    remember_id(id);
    return 0;
}

static int fake_ok(IMessageGateway *self) { (void)self; return 0; }
static void fake_notify(INotifier *self, const Notification *n) { (void)self; (void)n; }

static void store(IMessageStore *s, const char *id, MessageType type, const char *text, const char *path, const char *ref,
                  int deleted, int forwarded) {
    Message m;
    message_init(&m);
    str_copy(m.id, sizeof(m.id), id);
    str_copy(m.chat_jid, sizeof(m.chat_jid), "27820000001@s.whatsapp.net");
    str_copy(m.sender_jid, sizeof(m.sender_jid), "27820000001@s.whatsapp.net");
    m.type = type;
    m.timestamp = 1790000000;
    m.deleted = deleted;
    m.forwarded = forwarded;
    if (text) message_set_text(&m, text);
    if (path) str_copy(m.media_path, sizeof(m.media_path), path);
    if (ref) message_set_media_ref(&m, ref);
    s->save(s, &m);
    message_dispose(&m);
}

static void test_manager(const char *dir) {
    char db_path[600], media_dir[600], photo[700];
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", dir);
    snprintf(media_dir, sizeof(media_dir), "%s/media", dir);
    snprintf(photo, sizeof(photo), "%s/photo.jpg", media_dir);
    mkdir(media_dir, 0700);
    FILE *f = fopen(photo, "w");
    if (f) { fputs("\xFF\xD8\xFF picture", f); fclose(f); }

    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) { failures++; return; }
    Settings settings;
    settings_set_defaults(&settings);
    str_copy(settings.media_dir, sizeof(settings.media_dir), media_dir);
    IMessageGateway gw;
    memset(&gw, 0, sizeof(gw));
    gw.start = fake_ok;
    gw.connect = fake_ok;
    gw.send_text = fake_send_text;
    gw.send_media = fake_send_media;
    gw.forward_media = fake_forward_media;
    INotifier notifier = { NULL, fake_notify, NULL };
    EventQueue *events = event_queue_create(16);
    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    IChatStore *chats = sqlite_chat_store_create(db, ACCOUNT_ID_FIRST);
    IContactStore *contacts = sqlite_contact_store_create(db, ACCOUNT_ID_FIRST);
    IJidAliasStore *aliases = sqlite_jid_alias_store_create(db, ACCOUNT_ID_FIRST);
    IReactionStore *reactions = sqlite_reaction_store_create(db, ACCOUNT_ID_FIRST);
    IReceiptStore *receipts = sqlite_receipt_store_create(db, ACCOUNT_ID_FIRST);
    IChatExporter *exporter = text_chat_exporter_create();
    MessagingManagerDeps deps = { &gw, messages, chats, contacts, aliases, reactions, receipts, &notifier, events,
                                  &settings, NULL, exporter, NULL, NULL };
    MessagingManager *m = messaging_manager_create(&deps);

    store(messages, "T1", MESSAGE_TYPE_TEXT, "See you at six", NULL, NULL, 0, 0);
    store(messages, "T2", MESSAGE_TYPE_TEXT, "Pass this on", NULL, NULL, 0, 1);
    store(messages, "P1", MESSAGE_TYPE_IMAGE, "The view", photo, "image:AAAA", 0, 0);
    store(messages, "R1", MESSAGE_TYPE_IMAGE, "", NULL, "image:BBBB", 0, 0);
    store(messages, "D1", MESSAGE_TYPE_TEXT, "gone", NULL, NULL, 1, 0);
    store(messages, "O1", MESSAGE_TYPE_OTHER, "\xF0\x9F\x93\x8A Poll", NULL, NULL, 0, 0);

    const char *two[] = { "27820000002@s.whatsapp.net", "120363000000000001@g.us" };
    CHECK(messaging_manager_forward(m, "T1", two, 2) == 2 && texts == 2, "text goes to each chat");
    CHECK(last_forwarded && last_score == 1, "marked as forwarded, for the first time");
    CHECK(strcmp(last_id[0], last_id[1]) != 0 && strcmp(last_id[0], "T1") != 0, "every copy is a message of its own");
    Message copy;
    CHECK(messages->get(messages, last_id[0], &copy) == 0 && copy.from_me && copy.forwarded &&
          strcmp(copy.chat_jid, "120363000000000001@g.us") == 0, "each copy is stored as yours, forwarded, in its chat");
    message_dispose(&copy);

    messaging_manager_forward(m, "T2", two, 1);
    CHECK(last_score == 2, "a message that was already forwarded counts on");

    CHECK(messaging_manager_forward(m, "P1", two, 1) == 1 && medias == 1 && last_forwarded, "a downloaded photo is sent again");
    CHECK(strstr(last_path, "/outgoing/") != NULL && access(last_path, R_OK) == 0, "from its own copy in the outgoing folder");

    CHECK(messaging_manager_forward(m, "R1", two, 1) == 1 && refs == 1 && strcmp(last_ref, "image:BBBB") == 0,
          "a photo that is not downloaded goes on by reference");

    int before = texts + medias + refs;
    CHECK(messaging_manager_forward(m, "D1", two, 2) == 0 && messaging_manager_forward(m, "O1", two, 2) == 0 &&
          texts + medias + refs == before, "deleted messages and polls are not forwarded");
    CHECK(messaging_manager_forward(m, "nope", two, 1) == -1, "an unknown message is refused");

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
}

/* ---- matching and the picker ------------------------------------------------ */

static Chat make_chat(const char *jid, const char *name, int locked) {
    Chat c;
    chat_init(&c, jid);
    str_copy(c.name, sizeof(c.name), name);
    c.is_locked = locked;
    return c;
}

static void test_match(void) {
    Chat mom = make_chat("27820000001@s.whatsapp.net", "Mom", 0);
    Chat secret = make_chat("27820000009@s.whatsapp.net", "Secret", 1);
    CHECK(chat_match_filter(&mom, "") && chat_match_filter(&mom, "mo") && chat_match_filter(&mom, "2782000000"),
          "a chat matches by name, in any case, or by number");
    CHECK(!chat_match_filter(&mom, "dad"), "and not otherwise");
    CHECK(!chat_match_searchable(&secret, 0) && chat_match_searchable(&secret, 1) && chat_match_searchable(&mom, 0),
          "locked chats only show inside the Locked folder");
}

static void test_picker(void) {
    Chat chats[8];
    const char *names[8] = { "Mom", "Sarah", "Dev team", "Pieter", "Book club", "Nadia", "Jan", "Secret" };
    for (int i = 0; i < 8; i++) {
        char jid[64];
        snprintf(jid, sizeof(jid), "2782000000%d@s.whatsapp.net", i);
        chats[i] = make_chat(jid, names[i], i == 7);
    }
    ChatPicker p;
    chat_picker_open(&p, "Forward to");
    chat_picker_render(&p, (UiRect){ 1, 0, 30, 100 }, chats, 8);
    CHECK(p.row_count == 7, "locked chats are not offered");
    for (int i = 0; i < 6; i++) {
        chat_picker_key(&p, 0, ' ');
        chat_picker_key(&p, 1, KEY_DOWN);
    }
    CHECK(p.chosen_count == CHAT_PICKER_MAX_CHOSEN && p.full, "at most five chats, and it says so");
    chat_picker_key(&p, 1, KEY_HOME);
    chat_picker_render(&p, (UiRect){ 1, 0, 30, 100 }, chats, 8);
    CHECK(strcmp(p.row_jid[0], chats[0].jid) == 0, "chosen chats stay at the top");
    p.selected = 0;
    chat_picker_key(&p, 0, ' ');
    CHECK(p.chosen_count == 4 && !chat_picker_is_chosen(&p, chats[0].jid), "Space unticks a chosen chat");

    chat_picker_open(&p, "Forward to");
    chat_picker_key(&p, 0, 'd');
    chat_picker_key(&p, 0, 'e');
    chat_picker_key(&p, 0, 'v');
    chat_picker_render(&p, (UiRect){ 1, 0, 30, 100 }, chats, 8);
    CHECK(p.row_count == 1 && strcmp(p.row_jid[0], chats[2].jid) == 0, "typing filters the chats");
    CHECK(chat_picker_key(&p, 0, '\n') == POPUP_CHOSEN && p.chosen_count == 1 && !p.open,
          "Enter with nothing ticked sends to the highlighted chat");
    const char *out[CHAT_PICKER_MAX_CHOSEN];
    CHECK(chat_picker_chosen(&p, out) == 1 && strcmp(out[0], chats[2].jid) == 0, "which is handed over");
    chat_picker_open(&p, "Forward to");
    CHECK(chat_picker_key(&p, 0, 27) == POPUP_CLOSED && !p.open, "Esc closes it");
}

int main(void) {
    setlocale(LC_ALL, "");
    char dir[] = "/tmp/tawk-forward-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    FILE *out = fopen("/dev/null", "w"), *in = fopen("/dev/null", "r");
    SCREEN *screen = out && in ? newterm("xterm-256color", out, in) : NULL;
    if (!screen) { fprintf(stderr, "FAIL: no curses screen\n"); return 1; }
    resizeterm(30, 100);
    tui_palette_init();
    test_manager(dir);
    test_match();
    test_picker();
    endwin();
    delscreen(screen);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures == 0) printf("ok: messages are forwarded to up to five chats, as copies marked as forwarded\n");
    return failures != 0;
}
