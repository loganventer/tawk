/* Viewing statuses: the status store, the status feed manager fed through
 * its observer, and the background colour on message events. */
#include "core/event.h"
#include "managers/status_feed_manager.h"
#include "resource_access/json_protocol.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_status_store.h"
#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_receipt_store.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static int64_t now_s(void) { return (int64_t)time(NULL); }

static void put(IStatusStore *s, const char *id, const char *author, int64_t ts, int from_me, const char *media_path) {
    StatusUpdate u;
    status_update_init(&u);
    str_copy(u.id, sizeof(u.id), id);
    str_copy(u.author_jid, sizeof(u.author_jid), author);
    str_copy(u.author_name, sizeof(u.author_name), author);
    u.type = MESSAGE_TYPE_TEXT;
    u.text = "hello";
    u.background_argb = 0xFF00897BU;
    u.timestamp = ts;
    u.from_me = from_me;
    u.viewed = from_me;
    if (media_path) str_copy(u.media_path, sizeof(u.media_path), media_path);
    s->save(s, &u);
}

static void forget(void *ctx, const char *path) { (void)ctx; unlink(path); }

static void test_store(IStatusStore *s, const char *media_dir) {
    int64_t t = now_s();
    put(s, "A1", "alice@s.whatsapp.net", t - 300, 0, NULL);
    put(s, "A2", "alice@s.whatsapp.net", t - 100, 0, NULL);
    put(s, "B1", "bob@s.whatsapp.net", t - 50, 0, NULL);
    put(s, "B1", "someone@s.whatsapp.net", t, 0, NULL);          /* the same id again */
    put(s, "OLD", "carol@s.whatsapp.net", t - 90000, 0, NULL);

    StatusUpdate u;
    CHECK(s->get(s, "A1", &u) == 0 && u.text && strcmp(u.text, "hello") == 0 && u.background_argb == 0xFF00897BU,
          "a status is stored and read back with its background");
    status_update_dispose(&u);
    CHECK(s->get(s, "B1", &u) == 0 && strcmp(u.author_jid, "bob@s.whatsapp.net") == 0, "the first copy of an id is kept");
    status_update_dispose(&u);

    StatusAuthor authors[8];
    int n = s->authors(s, t - 86400, t + 86400, authors, 8);
    CHECK(n == 2 && strcmp(authors[0].jid, "bob@s.whatsapp.net") == 0 && strcmp(authors[1].jid, "alice@s.whatsapp.net") == 0,
          "authors are newest first, and a day-old status is left out");
    CHECK(n == 2 && authors[1].count == 2 && authors[1].unviewed == 2, "an author's statuses are counted");
    s->mark_viewed(s, "A1");
    n = s->authors(s, t - 86400, t + 86400, authors, 8);
    CHECK(n == 2 && authors[1].unviewed == 1, "viewing one leaves one unseen");

    StatusUpdate list[4];
    n = s->updates_by(s, "alice@s.whatsapp.net", t - 86400, t + 86400, list, 4);
    CHECK(n == 2 && strcmp(list[0].id, "A1") == 0 && strcmp(list[1].id, "A2") == 0, "an author's statuses are oldest first");
    status_update_array_free(list, n);

    char file[700];
    snprintf(file, sizeof(file), "%s/old.jpg", media_dir);
    FILE *f = fopen(file, "w");
    if (f) { fputs("x", f); fclose(f); }
    put(s, "OLD2", "carol@s.whatsapp.net", t - 90000, 0, file);
    CHECK(s->prune(s, t - 86400, forget, NULL) == 2 && access(file, F_OK) != 0, "pruning forgets old statuses and their files");
}

/* ---- the manager, with a fake backend ----------------------------------- */

static int downloads;
static int fake_download(IMessageGateway *self, const char *id, const char *ref, int max_mb) {
    (void)self; (void)id; (void)ref; (void)max_mb;
    downloads++;
    return 0;
}

static int deliver(StatusFeedManager *m, Event *e) {
    IEventObserver *o = status_feed_manager_observer(m);
    int used = o->on_event(o, e);
    event_dispose(e);
    return used;
}

static void status_message(Event *e, const char *id, const char *sender, int from_me, MessageType type) {
    event_init(e, EVENT_MESSAGE_UPSERT);
    str_copy(e->message.id, sizeof(e->message.id), id);
    str_copy(e->message.chat_jid, sizeof(e->message.chat_jid), "status@broadcast");
    str_copy(e->message.sender_jid, sizeof(e->message.sender_jid), sender);
    e->message.type = type;
    e->message.timestamp = now_s() - 10;
    e->message.from_me = from_me;
    message_set_text(&e->message, "caption");
    if (type != MESSAGE_TYPE_TEXT) message_set_media_ref(&e->message, "image:abc");
}

static void test_manager(IStatusStore *s, const char *media_dir) {
    IMessageGateway gw;
    memset(&gw, 0, sizeof(gw));
    gw.download_media = fake_download;
    Settings settings;
    settings_set_defaults(&settings);
    settings.auto_download_media = 0;
    StatusFeedManagerDeps deps = { s, &gw, media_dir, &settings, NULL, NULL };
    StatusFeedManager *m = status_feed_manager_create(&deps);

    Event e;
    event_init(&e, EVENT_AUTH_CONNECTED);
    str_copy(e.jid, sizeof(e.jid), "me@s.whatsapp.net");
    deliver(m, &e);

    status_message(&e, "S1", "dave@s.whatsapp.net", 0, MESSAGE_TYPE_IMAGE);
    deliver(m, &e);
    status_message(&e, "M1", "status@broadcast", 1, MESSAGE_TYPE_TEXT);   /* yours, without a sender */
    deliver(m, &e);
    CHECK(status_feed_manager_take_changed(m) && !status_feed_manager_take_changed(m), "a change is reported once");

    StatusAuthor authors[8];
    int n = status_feed_manager_authors(m, 0, authors, 8);
    CHECK(n >= 2 && authors[0].from_me && strcmp(authors[0].jid, "me@s.whatsapp.net") == 0, "your statuses come first, under your JID");
    CHECK(status_feed_manager_unviewed_authors(m) >= 1, "someone's status is unseen");

    status_feed_manager_fetch_media(m, "S1");
    status_feed_manager_fetch_media(m, "S1");
    CHECK(downloads == 1, "a photo is downloaded once while on its way");

    event_init(&e, EVENT_MEDIA_READY);
    str_copy(e.id, sizeof(e.id), "S1");
    str_copy(e.path, sizeof(e.path), "/etc/passwd");
    deliver(m, &e);
    StatusUpdate u;
    CHECK(s->get(s, "S1", &u) == 0 && u.media_path[0] == '\0', "a file outside the media folder is refused");
    status_update_dispose(&u);

    char file[700];
    snprintf(file, sizeof(file), "%s/S1.jpg", media_dir);
    FILE *f = fopen(file, "w");
    if (f) { fputs("jpeg", f); fclose(f); }
    event_init(&e, EVENT_MEDIA_READY);
    str_copy(e.id, sizeof(e.id), "S1");
    str_copy(e.path, sizeof(e.path), file);
    deliver(m, &e);
    CHECK(s->get(s, "S1", &u) == 0 && strcmp(u.media_path, file) == 0, "a downloaded photo is kept with its status");
    status_update_dispose(&u);

    event_init(&e, EVENT_MESSAGE_EDIT);
    str_copy(e.message.id, sizeof(e.message.id), "S1");
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), "status@broadcast");
    e.message.deleted = 1;
    deliver(m, &e);
    CHECK(s->get(s, "S1", &u) != 0, "a deleted status goes away");

    /* Older than a day: in the archive while kept, then gone. */
    status_message(&e, "OLD", "erin@s.whatsapp.net", 0, MESSAGE_TYPE_IMAGE);
    e.message.timestamp = now_s() - 3 * 86400;
    settings.auto_download_media = 1;
    int before = downloads;
    deliver(m, &e);
    CHECK(downloads == before + 1, "a status kept for the archive downloads its photo on arrival");
    StatusAuthor older[8];
    int recent = status_feed_manager_authors(m, 0, authors, 8), archived = status_feed_manager_authors(m, 1, older, 8);
    int in_recent = 0;
    for (int i = 0; i < recent; i++) in_recent |= strcmp(authors[i].jid, "erin@s.whatsapp.net") == 0;
    CHECK(!in_recent && archived == 1 && strcmp(older[0].jid, "erin@s.whatsapp.net") == 0, "a status older than a day is in the archive only");
    StatusUpdate kept[4];
    int k = status_feed_manager_updates(m, "erin@s.whatsapp.net", 1, kept, 4);
    CHECK(k == 1 && strcmp(kept[0].id, "OLD") == 0, "the archive shows that status");
    status_update_array_free(kept, k);
    settings.status_keep_days = 1;
    status_message(&e, "OLD2", "frank@s.whatsapp.net", 0, MESSAGE_TYPE_TEXT);
    e.message.timestamp = now_s() - 3 * 86400;
    deliver(m, &e);
    CHECK(s->get(s, "OLD2", &u) != 0, "with no archive, statuses older than a day are not kept");

    status_feed_manager_destroy(m);
    unlink(file);
}

static void test_protocol(void) {
    Event e;
    CHECK(json_protocol_decode("{\"evt\":\"message\",\"id\":\"X\",\"chat\":\"status@broadcast\",\"sender\":\"a@s.whatsapp.net\","
                               "\"type\":\"text\",\"text\":\"hi\",\"bg\":4278225019,\"path\":\"/tmp/x.jpg\"}", &e) == 0 &&
          e.message.background_argb == 4278225019U && strcmp(e.message.media_path, "/tmp/x.jpg") == 0,
          "a status's background and local file are decoded");
    event_dispose(&e);
    CHECK(json_protocol_decode("{\"evt\":\"message\",\"id\":\"Y\",\"chat\":\"a@s.whatsapp.net\",\"type\":\"text\",\"path\":\"/tmp/x.jpg\"}", &e) == 0 &&
          e.message.media_path[0] == '\0', "other messages never take a path from the backend");
    event_dispose(&e);
}

/* Who saw and liked your statuses: read receipts and heart reactions. */
static void test_viewers(sqlite3 *db, IStatusStore *s, const char *media_dir) {
    IReceiptStore *receipts = sqlite_receipt_store_create(db, ACCOUNT_ID_FIRST);
    IReactionStore *reactions = sqlite_reaction_store_create(db, ACCOUNT_ID_FIRST);
    IMessageGateway gw;
    memset(&gw, 0, sizeof(gw));
    Settings settings;
    settings_set_defaults(&settings);
    StatusFeedManagerDeps deps = { s, &gw, media_dir, &settings, receipts, reactions };
    StatusFeedManager *m = status_feed_manager_create(&deps);

    Event e;
    event_init(&e, EVENT_AUTH_CONNECTED);
    str_copy(e.jid, sizeof(e.jid), "me@s.whatsapp.net");
    deliver(m, &e);
    status_message(&e, "MINE", "me@s.whatsapp.net", 1, MESSAGE_TYPE_TEXT);
    deliver(m, &e);
    status_message(&e, "THEIRS", "dave@s.whatsapp.net", 0, MESSAGE_TYPE_TEXT);
    deliver(m, &e);

    int64_t t = now_s();
    receipts->put(receipts, "MINE", "ann@s.whatsapp.net", RECEIPT_READ, t - 60);
    receipts->put(receipts, "MINE", "bob@s.whatsapp.net", RECEIPT_READ, t - 30);
    receipts->put(receipts, "MINE", "cat@s.whatsapp.net", RECEIPT_DELIVERED, t - 20);
    event_init(&e, EVENT_REACTION);
    str_copy(e.id, sizeof(e.id), "MINE");
    str_copy(e.chat.jid, sizeof(e.chat.jid), "status@broadcast");
    str_copy(e.jid, sizeof(e.jid), "ann@s.whatsapp.net");
    str_copy(e.emoji, sizeof(e.emoji), "\xE2\x9D\xA4\xEF\xB8\x8F");
    CHECK(deliver(m, &e), "a like of your status is kept");
    str_copy(e.jid, sizeof(e.jid), "eve@s.whatsapp.net");
    deliver(m, &e);
    str_copy(e.id, sizeof(e.id), "THEIRS");
    CHECK(!deliver(m, &e), "likes of someone else's status are not yours to keep");

    StatusViewer v[8];
    int n = status_feed_manager_viewers(m, "MINE", v, 8);
    CHECK(n == 3, "viewers are those who read it, and a like whose view has not come yet");
    CHECK(n == 3 && strcmp(v[0].jid, "bob@s.whatsapp.net") == 0 && strcmp(v[1].jid, "ann@s.whatsapp.net") == 0,
          "the most recent viewer comes first");
    CHECK(n == 3 && v[1].reaction[0] && !v[0].reaction[0], "a heart shows beside those who liked it");
    StatusLike like;
    CHECK(status_feed_manager_take_like(m, &like) == 0 && strcmp(like.who, "ann@s.whatsapp.net") == 0,
          "each new like is reported once");
    status_feed_manager_take_like(m, &like);
    CHECK(status_feed_manager_take_like(m, &like) != 0, "and only once");

    status_feed_manager_destroy(m);
    reactions->destroy(reactions);
    receipts->destroy(receipts);
}

int main(void) {
    char dir[] = "/tmp/tawk-status-test-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    char db_path[600], media[600];
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", dir);
    snprintf(media, sizeof(media), "%s/media", dir);
    mkdir(media, 0700);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    CHECK(db != NULL, "the database opens and migrates");
    if (!db) return 1;
    IStatusStore *s = sqlite_status_store_create(db, ACCOUNT_ID_FIRST);
    test_store(s, media);
    test_manager(s, media);
    test_protocol();
    test_viewers(db, s, media);
    s->destroy(s);
    sqlite_database_close(db);
    unlink(db_path);
    rmdir(media);
    rmdir(dir);
    if (failures == 0) printf("ok: statuses are kept for a day, fetched when looked at and forgotten after\n");
    return failures != 0;
}
