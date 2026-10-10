/* Upgrading an existing database: a v9 database (tawk 0.6) and a v16 one
 * (the last before several accounts) opened by this version keep every row,
 * gain the new columns with their defaults, and a copy of each as it was is
 * kept beside it. What was there becomes the first account. A failed upgrade
 * changes nothing. */
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_message_store.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define LATEST 24

static int scalar(sqlite3 *db, const char *sql) {
    sqlite3_stmt *st = NULL;
    int v = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return v;
}

/* The tables as tawk 0.6 made them, written out: a database of that time
 * cannot be had by taking columns away from today's. */
static const char V9_SCHEMA[] =
    "CREATE TABLE messages ("
    "  id TEXT PRIMARY KEY, chat_jid TEXT NOT NULL, sender_jid TEXT NOT NULL DEFAULT '',"
    "  sender_name TEXT NOT NULL DEFAULT '', text TEXT, media_ref TEXT, media_path TEXT NOT NULL DEFAULT '',"
    "  type INTEGER NOT NULL DEFAULT 0, status INTEGER NOT NULL DEFAULT 0,"
    "  ts INTEGER NOT NULL DEFAULT 0, from_me INTEGER NOT NULL DEFAULT 0, duration INTEGER NOT NULL DEFAULT 0,"
    "  quoted_id TEXT NOT NULL DEFAULT '', quoted_sender TEXT NOT NULL DEFAULT '', quoted_text TEXT, thumbnail BLOB,"
    "  edited INTEGER NOT NULL DEFAULT 0, deleted INTEGER NOT NULL DEFAULT 0);"
    "CREATE INDEX idx_messages_chat_ts ON messages(chat_jid, ts);"
    "CREATE INDEX idx_messages_sender ON messages(sender_jid);"
    "CREATE TABLE chats ("
    "  jid TEXT PRIMARY KEY, name TEXT NOT NULL DEFAULT '', preview TEXT NOT NULL DEFAULT '',"
    "  last_ts INTEGER NOT NULL DEFAULT 0, unread INTEGER NOT NULL DEFAULT 0,"
    "  is_group INTEGER NOT NULL DEFAULT 0, is_muted INTEGER NOT NULL DEFAULT 0, is_pinned INTEGER NOT NULL DEFAULT 0,"
    "  is_archived INTEGER NOT NULL DEFAULT 0, is_locked INTEGER NOT NULL DEFAULT 0, muted_until INTEGER NOT NULL DEFAULT 0,"
    "  tone TEXT NOT NULL DEFAULT '', draft TEXT NOT NULL DEFAULT '', theme TEXT NOT NULL DEFAULT '',"
    "  soft_locked INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE contacts (jid TEXT PRIMARY KEY, name TEXT NOT NULL DEFAULT '', push_name TEXT NOT NULL DEFAULT '');"
    "CREATE TABLE jid_aliases (alias TEXT PRIMARY KEY, canonical TEXT NOT NULL);"
    "CREATE TABLE reactions ("
    "  message_id TEXT NOT NULL, sender_jid TEXT NOT NULL, emoji TEXT NOT NULL, PRIMARY KEY (message_id, sender_jid));"
    "CREATE TABLE profiles ("
    "  jid TEXT PRIMARY KEY, about TEXT NOT NULL DEFAULT '', verified_name TEXT NOT NULL DEFAULT '',"
    "  is_business INTEGER NOT NULL DEFAULT 0, business_category TEXT NOT NULL DEFAULT '',"
    "  business_address TEXT NOT NULL DEFAULT '', business_email TEXT NOT NULL DEFAULT '',"
    "  is_group INTEGER NOT NULL DEFAULT 0, group_subject TEXT NOT NULL DEFAULT '',"
    "  group_description TEXT NOT NULL DEFAULT '', group_owner TEXT NOT NULL DEFAULT '',"
    "  group_created INTEGER NOT NULL DEFAULT 0, participant_count INTEGER NOT NULL DEFAULT 0,"
    "  participants TEXT, picture TEXT NOT NULL DEFAULT '', picture_full TEXT NOT NULL DEFAULT '',"
    "  picture_none INTEGER NOT NULL DEFAULT 0, blocked INTEGER NOT NULL DEFAULT 0,"
    "  fetched_at INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE message_receipts ("
    "  message_id TEXT NOT NULL, jid TEXT NOT NULL,"
    "  delivered_at INTEGER NOT NULL DEFAULT 0, read_at INTEGER NOT NULL DEFAULT 0, played_at INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY (message_id, jid));"
    "CREATE TABLE statuses ("
    "  id TEXT PRIMARY KEY, author_jid TEXT NOT NULL, author_name TEXT NOT NULL DEFAULT '',"
    "  type INTEGER NOT NULL DEFAULT 0, text TEXT, media_ref TEXT, media_path TEXT NOT NULL DEFAULT '',"
    "  thumbnail BLOB, background_argb INTEGER NOT NULL DEFAULT 0, timestamp INTEGER NOT NULL DEFAULT 0,"
    "  from_me INTEGER NOT NULL DEFAULT 0, viewed INTEGER NOT NULL DEFAULT 0);"
    "CREATE INDEX idx_statuses_author_ts ON statuses(author_jid, timestamp);"
    "CREATE INDEX idx_statuses_ts ON statuses(timestamp);";

/* What versions 10 to 16 added to that. */
static const char V10_TO_V16[] =
    "ALTER TABLE messages ADD COLUMN mentions TEXT;"
    "ALTER TABLE messages ADD COLUMN mentions_me INTEGER NOT NULL DEFAULT 0;"
    "ALTER TABLE chats ADD COLUMN unread_mention INTEGER NOT NULL DEFAULT 0;"
    "ALTER TABLE messages ADD COLUMN forwarded INTEGER NOT NULL DEFAULT 0;"
    "CREATE TABLE scheduled_messages ("
    "  id TEXT PRIMARY KEY, chat_jid TEXT NOT NULL, text TEXT NOT NULL, mentions TEXT,"
    "  quoted_id TEXT NOT NULL DEFAULT '', due_at INTEGER NOT NULL, created_at INTEGER NOT NULL,"
    "  state INTEGER NOT NULL DEFAULT 0);"
    "CREATE INDEX idx_scheduled_due ON scheduled_messages(state, due_at);"
    "ALTER TABLE messages ADD COLUMN link_url TEXT;"
    "ALTER TABLE messages ADD COLUMN link_title TEXT;"
    "ALTER TABLE messages ADD COLUMN link_desc TEXT;"
    "ALTER TABLE messages ADD COLUMN quoted_status INTEGER NOT NULL DEFAULT 0;"
    "CREATE TABLE automation_log ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, origin TEXT NOT NULL, client TEXT NOT NULL DEFAULT '',"
    "  op TEXT NOT NULL, chat_jid TEXT NOT NULL DEFAULT '', summary TEXT NOT NULL DEFAULT '', outcome TEXT NOT NULL);"
    "CREATE INDEX idx_automation_at ON automation_log(at);"
    "CREATE INDEX idx_reactions_sender ON reactions(sender_jid);"
    "CREATE INDEX idx_receipts_jid ON message_receipts(jid);";

static const char TWO_MESSAGES[] =
    "INSERT INTO chats (jid, name) VALUES ('27820000001@s.whatsapp.net', 'Mom');"
    "INSERT INTO messages (id, chat_jid, sender_jid, text, ts, from_me) VALUES"
    " ('A1', '27820000001@s.whatsapp.net', '27820000001@s.whatsapp.net', 'Hello *there*', 1790000000, 0),"
    " ('A2', '27820000001@s.whatsapp.net', 'me@s.whatsapp.net', 'Hi Mom', 1790000060, 1);";

static sqlite3 *raw_create(const char *path) {
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) { failures++; return NULL; }
    return db;
}

static void run(sqlite3 *db, const char *sql, const char *what) {
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "FAIL: %s: %s\n", what, err ? err : "?");
        failures++;
    }
    sqlite3_free(err);
}

/* A database as tawk 0.6 left it. */
static void make_v9(const char *path, int with_mentions_column) {
    sqlite3 *db = raw_create(path);
    if (!db) return;
    run(db, V9_SCHEMA, "the v9 tables");
    if (with_mentions_column) run(db, "ALTER TABLE messages ADD COLUMN mentions TEXT;", "a column version 10 will trip over");
    run(db, TWO_MESSAGES, "the v9 rows");
    run(db, "PRAGMA user_version = 9;", "the v9 version");
    sqlite3_close(db);
}

/* A database as the last version with one account left it, with a row in every table. */
static void make_v16(const char *path) {
    sqlite3 *db = raw_create(path);
    if (!db) return;
    run(db, V9_SCHEMA, "the v9 tables");
    run(db, V10_TO_V16, "what versions 10 to 16 added");
    run(db, TWO_MESSAGES, "the messages");
    run(db,
        "UPDATE chats SET unread = 3, is_pinned = 1, draft = 'half a thought', unread_mention = 1;"
        "INSERT INTO contacts (jid, name, push_name) VALUES ('27820000001@s.whatsapp.net', 'Mom', 'Mother');"
        "INSERT INTO jid_aliases (alias, canonical) VALUES ('1234@lid', '27820000001@s.whatsapp.net');"
        "INSERT INTO reactions (message_id, sender_jid, emoji) VALUES ('A2', '27820000001@s.whatsapp.net', 'x');"
        "INSERT INTO message_receipts (message_id, jid, delivered_at, read_at) VALUES ('A2', '27820000001@s.whatsapp.net', 5, 6);"
        "INSERT INTO profiles (jid, about, blocked) VALUES ('27820000001@s.whatsapp.net', 'Busy', 1);"
        "INSERT INTO statuses (id, author_jid, text, timestamp) VALUES ('S1', '27820000001@s.whatsapp.net', 'On holiday', 1790000100);"
        "INSERT INTO scheduled_messages (id, chat_jid, text, due_at, created_at) VALUES ('W1', '27820000001@s.whatsapp.net', 'later', 1790009999, 1790000000);"
        "INSERT INTO automation_log (at, origin, op, outcome) VALUES (1790000000, 'mcp', 'send_message', 'allowed');",
        "a row in every table");
    run(db, "PRAGMA user_version = 16;", "the v16 version");
    sqlite3_close(db);
}

static void test_upgrade(const char *dir) {
    char path[600], copy[700];
    snprintf(path, sizeof(path), "%s/tawk.db", dir);
    snprintf(copy, sizeof(copy), "%s.pre-v10", path);
    make_v9(path, 0);

    sqlite3 *db = sqlite_database_open(path, NULL);
    CHECK(db != NULL, "a v9 database opens and upgrades");
    if (!db) return;
    CHECK(scalar(db, "PRAGMA user_version") == LATEST, "it is now the latest version");
    CHECK(scalar(db, "SELECT count(*) FROM messages") == 2 && scalar(db, "SELECT count(*) FROM chats") == 1, "every row survives");
    CHECK(scalar(db, "SELECT count(*) FROM messages WHERE mentions IS NULL AND mentions_me = 0 AND forwarded = 0 AND link_url IS NULL") == 2,
          "old messages get the new columns with their defaults");
    CHECK(scalar(db, "SELECT unread_mention FROM chats") == 0, "chats get the new column too");
    CHECK(scalar(db, "SELECT count(*) FROM scheduled_messages") == 0, "the scheduled messages table exists");
    CHECK(scalar(db, "SELECT count(*) FROM automation_log") == 0, "the automation log table exists");
    CHECK(scalar(db, "SELECT count(*) FROM messages WHERE text = 'Hello *there*'") == 1, "text is kept as it was");
    sqlite_database_close(db);

    sqlite3 *old = NULL;
    CHECK(access(copy, F_OK) == 0 && sqlite3_open_v2(copy, &old, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK &&
          scalar(old, "PRAGMA user_version") == 9 && scalar(old, "SELECT count(*) FROM messages") == 2,
          "a copy of the v9 database is kept beside it");
    /* tawk 0.6 reads only the columns it knows; they are all still there. */
    CHECK(scalar(old, "SELECT count(*) FROM messages") == 2, "the copy opens as tawk 0.6 left it");
    sqlite3_close(old);

    db = sqlite_database_open(path, NULL);
    CHECK(db != NULL, "the upgraded database opens again");
    sqlite_database_close(db);
    sqlite3 *v9reader = NULL;
    sqlite3_open_v2(path, &v9reader, SQLITE_OPEN_READONLY, NULL);
    CHECK(scalar(v9reader, "SELECT count(*) FROM (SELECT id, chat_jid, sender_jid, sender_name, text, media_ref, media_path, type,"
                           " status, ts, from_me, duration, quoted_id, quoted_sender, quoted_text, thumbnail, edited, deleted FROM messages)") == 2,
          "tawk 0.6's column list still reads the upgraded database");
    sqlite3_close(v9reader);
    unlink(path);
    unlink(copy);
}

static void test_failed_upgrade(const char *dir) {
    char path[600], copy[700];
    snprintf(path, sizeof(path), "%s/broken.db", dir);
    snprintf(copy, sizeof(copy), "%s.pre-v10", path);
    make_v9(path, 1);                                  /* the mentions column already exists: version 10 fails */
    sqlite3 *db = sqlite_database_open(path, NULL);
    CHECK(db == NULL, "a failing upgrade refuses to open the database");
    sqlite3 *raw = NULL;
    sqlite3_open_v2(path, &raw, SQLITE_OPEN_READONLY, NULL);
    CHECK(scalar(raw, "PRAGMA user_version") == 9 && scalar(raw, "SELECT count(*) FROM messages") == 2,
          "and leaves it at version 9 with every row");
    sqlite3_close(raw);
    CHECK(access(copy, F_OK) == 0, "the copy from before the upgrade is there to fall back on");
    unlink(path);
    unlink(copy);
}

/* One account becomes the first of several. */
static void test_first_account(const char *dir) {
    char path[600], copy[700];
    snprintf(path, sizeof(path), "%s/single.db", dir);
    snprintf(copy, sizeof(copy), "%s.pre-v17", path);
    make_v16(path);

    sqlite3 *db = sqlite_database_open(path, NULL);
    CHECK(db != NULL, "a v16 database opens and upgrades");
    if (!db) return;
    CHECK(scalar(db, "PRAGMA user_version") == LATEST, "it is now the latest version");
    CHECK(scalar(db, "SELECT count(*) FROM accounts") == 1 &&
          scalar(db, "SELECT count(*) FROM accounts WHERE id = 1 AND label = 'main' AND is_primary = 1 AND agent_access = 'follow'") == 1,
          "what was there is the first account, primary, obeying the agent setting as before");
    /* Version 18: transcripts of voice notes, and a chat's own choices about them. */
    CHECK(scalar(db, "SELECT count(*) FROM transcripts") == 0, "there is a table for transcripts, empty");
    CHECK(scalar(db, "SELECT count(*) FROM pragma_table_info('chat_prefs') WHERE name IN ('show_transcripts', 'transcribe_off')") == 2,
          "and a chat's choices about them have their columns");
    CHECK(scalar(db, "SELECT count(*) FROM sqlite_master WHERE type = 'trigger' AND name LIKE 'transcripts_message_%'") == 2,
          "with the triggers that remove a transcript when its message goes");
    /* Version 19: TL;DR summaries, and a chat's switch for them. */
    CHECK(scalar(db, "SELECT count(*) FROM summaries") == 0 &&
          scalar(db, "SELECT count(*) FROM pragma_table_info('chat_prefs') WHERE name = 'tldr'") == 1 &&
          scalar(db, "SELECT count(*) FROM sqlite_master WHERE type = 'trigger' AND name LIKE 'summaries_message_%'") == 2,
          "there is a table for TL;DR summaries, a chat's switch for them, and the triggers that keep them honest");
    static const char *const TABLES[] = { "messages", "chats", "contacts", "jid_aliases", "reactions", "message_receipts",
                                          "profiles", "statuses", "scheduled_messages", "automation_log" };
    static const int ROWS[] = { 2, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    for (size_t i = 0; i < sizeof(TABLES) / sizeof(TABLES[0]); i++) {
        char sql[160], what[160];
        snprintf(sql, sizeof(sql), "SELECT count(*) FROM %s WHERE account_id = 1", TABLES[i]);
        snprintf(what, sizeof(what), "every row of %s is kept, in the first account", TABLES[i]);
        CHECK(scalar(db, sql) == ROWS[i], what);
        snprintf(sql, sizeof(sql), "SELECT count(*) FROM sqlite_master WHERE name = '%s_v16'", TABLES[i]);
        CHECK(scalar(db, sql) == 0, "nothing set aside is left behind");
    }
    CHECK(scalar(db, "SELECT count(*) FROM chats WHERE unread = 3 AND is_pinned = 1 AND draft = 'half a thought' AND unread_mention = 1") == 1,
          "a chat keeps its unread count, pin, draft and mention");
    CHECK(scalar(db, "SELECT count(*) FROM chat_prefs") == 0, "nobody has settings of their own yet");
    CHECK(scalar(db, "SELECT count(*) FROM message_receipts WHERE delivered_at = 5 AND read_at = 6") == 1, "a receipt keeps its times");
    CHECK(scalar(db, "SELECT count(*) FROM profiles WHERE about = 'Busy' AND blocked = 1") == 1, "a profile keeps its details");

    /* The same message can now be held once for each account. */
    run(db, "INSERT INTO messages (account_id, id, chat_jid, text) VALUES (2, 'A1', '27820000001@s.whatsapp.net', 'Hello *there*');",
        "the same message id in a second account");
    CHECK(scalar(db, "SELECT count(*) FROM messages WHERE id = 'A1'") == 2, "one message id can belong to two accounts");
    run(db, "DELETE FROM messages WHERE account_id = 2;", "taking it out again");

    IMessageStore *messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    Message *found = NULL;
    int count = 0;
    CHECK(messages && messages->search(messages, "mom", 10, &found, &count) == 0 && count == 1 && found && strcmp(found[0].id, "A2") == 0,
          "search still finds an old message after the tables were made again");
    for (int i = 0; i < count; i++) message_dispose(&found[i]);
    free(found);
    if (messages) messages->destroy(messages);
    sqlite_database_close(db);

    sqlite3 *old = NULL;
    CHECK(access(copy, F_OK) == 0 && sqlite3_open_v2(copy, &old, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK &&
          scalar(old, "PRAGMA user_version") == 16 && scalar(old, "SELECT count(*) FROM messages") == 2,
          "a copy of the v16 database is kept beside it");
    sqlite3_close(old);

    db = sqlite_database_open(path, NULL);
    CHECK(db != NULL && scalar(db, "SELECT count(*) FROM messages") == 2, "the upgraded database opens again with its rows");
    sqlite_database_close(db);
    unlink(path);
    unlink(copy);
}

/* A new install starts with one account too. */
static void test_new_database(const char *dir) {
    char path[600];
    snprintf(path, sizeof(path), "%s/new.db", dir);
    sqlite3 *db = sqlite_database_open(path, NULL);
    CHECK(db != NULL && scalar(db, "PRAGMA user_version") == LATEST, "a new database is made at the latest version");
    if (!db) return;
    CHECK(scalar(db, "SELECT count(*) FROM accounts WHERE id = 1 AND is_primary = 1") == 1, "it has its first account");
    CHECK(scalar(db, "SELECT count(*) FROM messages") == 0 && scalar(db, "SELECT count(*) FROM chats") == 0, "and nothing else");
    sqlite_database_close(db);
    char copy[700];
    snprintf(copy, sizeof(copy), "%s.pre-v17", path);
    CHECK(access(copy, F_OK) != 0, "a new database has no copy to keep");
    unlink(path);
}

int main(void) {
    char dir[] = "/tmp/tawk-migration-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    test_upgrade(dir);
    test_failed_upgrade(dir);
    test_first_account(dir);
    test_new_database(dir);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures == 0) printf("ok: old databases upgrade, keep their rows and a copy, become the first account, and a failed upgrade changes nothing\n");
    return failures != 0;
}
