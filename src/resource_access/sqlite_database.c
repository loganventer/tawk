#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_key.h"
#include "utilities/log.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Version 1: the original tables. Later versions are applied in order by
 * migrate(), so a database from any earlier release is upgraded in place. */
static const char *BASE_SCHEMA =
    "CREATE TABLE IF NOT EXISTS messages ("
    "  id TEXT PRIMARY KEY, chat_jid TEXT NOT NULL, sender_jid TEXT NOT NULL DEFAULT '',"
    "  sender_name TEXT NOT NULL DEFAULT '', text TEXT, media_ref TEXT, media_path TEXT NOT NULL DEFAULT '',"
    "  type INTEGER NOT NULL DEFAULT 0, status INTEGER NOT NULL DEFAULT 0,"
    "  ts INTEGER NOT NULL DEFAULT 0, from_me INTEGER NOT NULL DEFAULT 0,"
    "  duration INTEGER NOT NULL DEFAULT 0);"
    "CREATE INDEX IF NOT EXISTS idx_messages_chat_ts ON messages(chat_jid, ts);"
    "CREATE TABLE IF NOT EXISTS chats ("
    "  jid TEXT PRIMARY KEY, name TEXT NOT NULL DEFAULT '', preview TEXT NOT NULL DEFAULT '',"
    "  last_ts INTEGER NOT NULL DEFAULT 0, unread INTEGER NOT NULL DEFAULT 0,"
    "  is_group INTEGER NOT NULL DEFAULT 0, is_muted INTEGER NOT NULL DEFAULT 0,"
    "  is_pinned INTEGER NOT NULL DEFAULT 0);"
    "CREATE TABLE IF NOT EXISTS contacts ("
    "  jid TEXT PRIMARY KEY, name TEXT NOT NULL DEFAULT '', push_name TEXT NOT NULL DEFAULT '');";

/* One step of a version. A version with a lot to do is written as several
 * entries with the same number; they run in order inside one transaction. */
typedef struct Migration {
    int         version;
    const char *sql;
} Migration;

static const Migration MIGRATIONS[] = {
    { 2,
      "CREATE TABLE IF NOT EXISTS jid_aliases (alias TEXT PRIMARY KEY, canonical TEXT NOT NULL);"
      "CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender_jid);" },
    { 3,
      /* replies and inline previews */
      "ALTER TABLE messages ADD COLUMN quoted_id TEXT NOT NULL DEFAULT '';"
      "ALTER TABLE messages ADD COLUMN quoted_sender TEXT NOT NULL DEFAULT '';"
      "ALTER TABLE messages ADD COLUMN quoted_text TEXT;"
      "ALTER TABLE messages ADD COLUMN thumbnail BLOB;"
      /* chat folders, timed mutes, tones, drafts */
      "ALTER TABLE chats ADD COLUMN is_archived INTEGER NOT NULL DEFAULT 0;"
      "ALTER TABLE chats ADD COLUMN is_locked INTEGER NOT NULL DEFAULT 0;"
      "ALTER TABLE chats ADD COLUMN muted_until INTEGER NOT NULL DEFAULT 0;"
      "ALTER TABLE chats ADD COLUMN tone TEXT NOT NULL DEFAULT '';"
      "ALTER TABLE chats ADD COLUMN draft TEXT NOT NULL DEFAULT '';"
      "ALTER TABLE chats ADD COLUMN theme TEXT NOT NULL DEFAULT '';"
      "UPDATE chats SET muted_until = -1 WHERE is_muted = 1;"
      /* reactions */
      "CREATE TABLE IF NOT EXISTS reactions ("
      "  message_id TEXT NOT NULL, sender_jid TEXT NOT NULL, emoji TEXT NOT NULL,"
      "  PRIMARY KEY (message_id, sender_jid));"
      /* full-text search is set up by ensure_search_index, as some SQLite builds lack FTS5 */
      "SELECT 1;" },
    { 4,
      /* edited and deleted-for-everyone messages */
      "ALTER TABLE messages ADD COLUMN edited INTEGER NOT NULL DEFAULT 0;"
      "ALTER TABLE messages ADD COLUMN deleted INTEGER NOT NULL DEFAULT 0;" },
    { 5,
      /* soft-locked chats: the conversation stays blurred */
      "ALTER TABLE chats ADD COLUMN soft_locked INTEGER NOT NULL DEFAULT 0;" },
    { 6,
      /* contact and group details, profile pictures, the block list */
      "CREATE TABLE IF NOT EXISTS profiles ("
      "  jid TEXT PRIMARY KEY, about TEXT NOT NULL DEFAULT '', verified_name TEXT NOT NULL DEFAULT '',"
      "  is_business INTEGER NOT NULL DEFAULT 0, business_category TEXT NOT NULL DEFAULT '',"
      "  business_address TEXT NOT NULL DEFAULT '', business_email TEXT NOT NULL DEFAULT '',"
      "  is_group INTEGER NOT NULL DEFAULT 0, group_subject TEXT NOT NULL DEFAULT '',"
      "  group_description TEXT NOT NULL DEFAULT '', group_owner TEXT NOT NULL DEFAULT '',"
      "  group_created INTEGER NOT NULL DEFAULT 0, participant_count INTEGER NOT NULL DEFAULT 0,"
      "  participants TEXT, picture TEXT NOT NULL DEFAULT '', picture_full TEXT NOT NULL DEFAULT '',"
      "  picture_none INTEGER NOT NULL DEFAULT 0, blocked INTEGER NOT NULL DEFAULT 0,"
      "  fetched_at INTEGER NOT NULL DEFAULT 0);" },
    { 7,
      /* stored previews use the new photo and sticker icons (📷, 🔖) */
      "UPDATE chats SET preview = '\xF0\x9F\x93\xB7' || substr(preview, 2) WHERE substr(preview, 1, 1) = '\xF0\x9F\x96\xBC';"
      "UPDATE chats SET preview = '\xF0\x9F\x94\x96' || substr(preview, 2) WHERE substr(preview, 1, 1) = '\xF0\x9F\x8F\xB7';" },
    { 8,
      /* who received, read and played the messages you sent, and when (0: not yet) */
      "CREATE TABLE IF NOT EXISTS message_receipts ("
      "  message_id TEXT NOT NULL, jid TEXT NOT NULL,"
      "  delivered_at INTEGER NOT NULL DEFAULT 0, read_at INTEGER NOT NULL DEFAULT 0, played_at INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY (message_id, jid));" },
    { 9,
      /* statuses (yours and your contacts'), kept for a day */
      "CREATE TABLE IF NOT EXISTS statuses ("
      "  id TEXT PRIMARY KEY, author_jid TEXT NOT NULL, author_name TEXT NOT NULL DEFAULT '',"
      "  type INTEGER NOT NULL DEFAULT 0, text TEXT, media_ref TEXT, media_path TEXT NOT NULL DEFAULT '',"
      "  thumbnail BLOB, background_argb INTEGER NOT NULL DEFAULT 0, timestamp INTEGER NOT NULL DEFAULT 0,"
      "  from_me INTEGER NOT NULL DEFAULT 0, viewed INTEGER NOT NULL DEFAULT 0);"
      "CREATE INDEX IF NOT EXISTS idx_statuses_author_ts ON statuses(author_jid, timestamp);"
      "CREATE INDEX IF NOT EXISTS idx_statuses_ts ON statuses(timestamp);" },
    { 10,
      /* who a message mentions, and chats with an unread mention of you */
      "ALTER TABLE messages ADD COLUMN mentions TEXT;"
      "ALTER TABLE messages ADD COLUMN mentions_me INTEGER NOT NULL DEFAULT 0;"
      "ALTER TABLE chats ADD COLUMN unread_mention INTEGER NOT NULL DEFAULT 0;" },
    { 11,
      "ALTER TABLE messages ADD COLUMN forwarded INTEGER NOT NULL DEFAULT 0;" },
    { 12,
      /* messages to send later, kept here until they go */
      "CREATE TABLE IF NOT EXISTS scheduled_messages ("
      "  id TEXT PRIMARY KEY, chat_jid TEXT NOT NULL, text TEXT NOT NULL, mentions TEXT,"
      "  quoted_id TEXT NOT NULL DEFAULT '', due_at INTEGER NOT NULL, created_at INTEGER NOT NULL,"
      "  state INTEGER NOT NULL DEFAULT 0);"
      "CREATE INDEX IF NOT EXISTS idx_scheduled_due ON scheduled_messages(state, due_at);" },
    { 13,
      /* the card for a web address in a message (its picture is the thumbnail) */
      "ALTER TABLE messages ADD COLUMN link_url TEXT;"
      "ALTER TABLE messages ADD COLUMN link_title TEXT;"
      "ALTER TABLE messages ADD COLUMN link_desc TEXT;" },
    { 14,
      /* replies to a status: quoted_id is then the status's id */
      "ALTER TABLE messages ADD COLUMN quoted_status INTEGER NOT NULL DEFAULT 0;" },
    { 15,
      /* what programs reaching tawk through the control socket did (/automation) */
      "CREATE TABLE IF NOT EXISTS automation_log ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, origin TEXT NOT NULL, client TEXT NOT NULL DEFAULT '',"
      "  op TEXT NOT NULL, chat_jid TEXT NOT NULL DEFAULT '', summary TEXT NOT NULL DEFAULT '', outcome TEXT NOT NULL);"
      "CREATE INDEX IF NOT EXISTS idx_automation_at ON automation_log(at);" },
    { 16,
      /* a contact changing its address rewrites these by who, not by message */
      "CREATE INDEX IF NOT EXISTS idx_reactions_sender ON reactions(sender_jid);"
      "CREATE INDEX IF NOT EXISTS idx_receipts_jid ON message_receipts(jid);" },
    { 17,
      /* Several accounts in one database. What was here becomes the first
       * account. The same message reaches two accounts in a shared group with
       * the same id, so the account joins every key, and a key cannot be
       * altered: each table is set aside, made again and filled from the old
       * one. messages keeps its rowids, which the search index is tied to. */
      "CREATE TABLE accounts ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT, label TEXT NOT NULL, jid TEXT NOT NULL DEFAULT '',"
      "  name TEXT NOT NULL DEFAULT '', colour INTEGER NOT NULL DEFAULT 0, is_primary INTEGER NOT NULL DEFAULT 0,"
      "  agent_access TEXT NOT NULL DEFAULT 'off', self_approval_chats TEXT NOT NULL DEFAULT '',"
      "  last_chat TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL DEFAULT 0);"
      /* 'follow': the first account keeps obeying [automation] access, as it did before */
      "INSERT INTO accounts (id, label, is_primary, agent_access, created_at)"
      "  VALUES (1, 'main', 1, 'follow', CAST(strftime('%s', 'now') AS INTEGER));"

      "DROP TRIGGER IF EXISTS messages_fts_ai; DROP TRIGGER IF EXISTS messages_fts_ad; DROP TRIGGER IF EXISTS messages_fts_au;"
      "CREATE TABLE messages_v16 AS SELECT rowid AS old_rowid, * FROM messages;"
      "DROP TABLE messages;"
      "CREATE TABLE messages ("
      "  account_id INTEGER NOT NULL DEFAULT 1, id TEXT NOT NULL, chat_jid TEXT NOT NULL, sender_jid TEXT NOT NULL DEFAULT '',"
      "  sender_name TEXT NOT NULL DEFAULT '', text TEXT, media_ref TEXT, media_path TEXT NOT NULL DEFAULT '',"
      "  type INTEGER NOT NULL DEFAULT 0, status INTEGER NOT NULL DEFAULT 0,"
      "  ts INTEGER NOT NULL DEFAULT 0, from_me INTEGER NOT NULL DEFAULT 0, duration INTEGER NOT NULL DEFAULT 0,"
      "  quoted_id TEXT NOT NULL DEFAULT '', quoted_sender TEXT NOT NULL DEFAULT '', quoted_text TEXT, thumbnail BLOB,"
      "  edited INTEGER NOT NULL DEFAULT 0, deleted INTEGER NOT NULL DEFAULT 0, mentions TEXT,"
      "  mentions_me INTEGER NOT NULL DEFAULT 0, forwarded INTEGER NOT NULL DEFAULT 0,"
      "  link_url TEXT, link_title TEXT, link_desc TEXT, quoted_status INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY (account_id, id));"
      "INSERT INTO messages (rowid, account_id, id, chat_jid, sender_jid, sender_name, text, media_ref, media_path, type, status,"
      "  ts, from_me, duration, quoted_id, quoted_sender, quoted_text, thumbnail, edited, deleted, mentions, mentions_me,"
      "  forwarded, link_url, link_title, link_desc, quoted_status)"
      " SELECT old_rowid, 1, id, chat_jid, sender_jid, sender_name, text, media_ref, media_path, type, status,"
      "  ts, from_me, duration, quoted_id, quoted_sender, quoted_text, thumbnail, edited, deleted, mentions, mentions_me,"
      "  forwarded, link_url, link_title, link_desc, quoted_status FROM messages_v16;"
      "DROP TABLE messages_v16;"
      "CREATE INDEX idx_messages_chat_ts ON messages(account_id, chat_jid, ts);"
      "CREATE INDEX idx_messages_sender ON messages(account_id, sender_jid);" },
    { 17,
      "CREATE TABLE chats_v16 AS SELECT * FROM chats;"
      "DROP TABLE chats;"
      "CREATE TABLE chats ("
      "  account_id INTEGER NOT NULL DEFAULT 1, jid TEXT NOT NULL, name TEXT NOT NULL DEFAULT '', preview TEXT NOT NULL DEFAULT '',"
      "  last_ts INTEGER NOT NULL DEFAULT 0, unread INTEGER NOT NULL DEFAULT 0,"
      "  is_group INTEGER NOT NULL DEFAULT 0, is_muted INTEGER NOT NULL DEFAULT 0, is_pinned INTEGER NOT NULL DEFAULT 0,"
      "  is_archived INTEGER NOT NULL DEFAULT 0, is_locked INTEGER NOT NULL DEFAULT 0, muted_until INTEGER NOT NULL DEFAULT 0,"
      "  tone TEXT NOT NULL DEFAULT '', draft TEXT NOT NULL DEFAULT '', theme TEXT NOT NULL DEFAULT '',"
      "  soft_locked INTEGER NOT NULL DEFAULT 0, unread_mention INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY (account_id, jid));"
      "INSERT INTO chats (account_id, jid, name, preview, last_ts, unread, is_group, is_muted, is_pinned, is_archived, is_locked,"
      "  muted_until, tone, draft, theme, soft_locked, unread_mention)"
      " SELECT 1, jid, name, preview, last_ts, unread, is_group, is_muted, is_pinned, is_archived, is_locked,"
      "  muted_until, tone, draft, theme, soft_locked, unread_mention FROM chats_v16;"
      "DROP TABLE chats_v16;"

      "CREATE TABLE contacts_v16 AS SELECT * FROM contacts;"
      "DROP TABLE contacts;"
      "CREATE TABLE contacts ("
      "  account_id INTEGER NOT NULL DEFAULT 1, jid TEXT NOT NULL, name TEXT NOT NULL DEFAULT '', push_name TEXT NOT NULL DEFAULT '',"
      "  PRIMARY KEY (account_id, jid));"
      "INSERT INTO contacts (account_id, jid, name, push_name) SELECT 1, jid, name, push_name FROM contacts_v16;"
      "DROP TABLE contacts_v16;"

      "CREATE TABLE jid_aliases_v16 AS SELECT * FROM jid_aliases;"
      "DROP TABLE jid_aliases;"
      "CREATE TABLE jid_aliases ("
      "  account_id INTEGER NOT NULL DEFAULT 1, alias TEXT NOT NULL, canonical TEXT NOT NULL, PRIMARY KEY (account_id, alias));"
      "INSERT INTO jid_aliases (account_id, alias, canonical) SELECT 1, alias, canonical FROM jid_aliases_v16;"
      "DROP TABLE jid_aliases_v16;" },
    { 17,
      "CREATE TABLE reactions_v16 AS SELECT * FROM reactions;"
      "DROP TABLE reactions;"
      "CREATE TABLE reactions ("
      "  account_id INTEGER NOT NULL DEFAULT 1, message_id TEXT NOT NULL, sender_jid TEXT NOT NULL, emoji TEXT NOT NULL,"
      "  PRIMARY KEY (account_id, message_id, sender_jid));"
      "INSERT INTO reactions (account_id, message_id, sender_jid, emoji) SELECT 1, message_id, sender_jid, emoji FROM reactions_v16;"
      "DROP TABLE reactions_v16;"
      "CREATE INDEX idx_reactions_sender ON reactions(account_id, sender_jid);"

      "CREATE TABLE message_receipts_v16 AS SELECT * FROM message_receipts;"
      "DROP TABLE message_receipts;"
      "CREATE TABLE message_receipts ("
      "  account_id INTEGER NOT NULL DEFAULT 1, message_id TEXT NOT NULL, jid TEXT NOT NULL,"
      "  delivered_at INTEGER NOT NULL DEFAULT 0, read_at INTEGER NOT NULL DEFAULT 0, played_at INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY (account_id, message_id, jid));"
      "INSERT INTO message_receipts (account_id, message_id, jid, delivered_at, read_at, played_at)"
      " SELECT 1, message_id, jid, delivered_at, read_at, played_at FROM message_receipts_v16;"
      "DROP TABLE message_receipts_v16;"
      "CREATE INDEX idx_receipts_jid ON message_receipts(account_id, jid);" },
    { 17,
      "CREATE TABLE profiles_v16 AS SELECT * FROM profiles;"
      "DROP TABLE profiles;"
      "CREATE TABLE profiles ("
      "  account_id INTEGER NOT NULL DEFAULT 1, jid TEXT NOT NULL, about TEXT NOT NULL DEFAULT '', verified_name TEXT NOT NULL DEFAULT '',"
      "  is_business INTEGER NOT NULL DEFAULT 0, business_category TEXT NOT NULL DEFAULT '',"
      "  business_address TEXT NOT NULL DEFAULT '', business_email TEXT NOT NULL DEFAULT '',"
      "  is_group INTEGER NOT NULL DEFAULT 0, group_subject TEXT NOT NULL DEFAULT '',"
      "  group_description TEXT NOT NULL DEFAULT '', group_owner TEXT NOT NULL DEFAULT '',"
      "  group_created INTEGER NOT NULL DEFAULT 0, participant_count INTEGER NOT NULL DEFAULT 0,"
      "  participants TEXT, picture TEXT NOT NULL DEFAULT '', picture_full TEXT NOT NULL DEFAULT '',"
      "  picture_none INTEGER NOT NULL DEFAULT 0, blocked INTEGER NOT NULL DEFAULT 0,"
      "  fetched_at INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (account_id, jid));"
      "INSERT INTO profiles (account_id, jid, about, verified_name, is_business, business_category, business_address,"
      "  business_email, is_group, group_subject, group_description, group_owner, group_created, participant_count,"
      "  participants, picture, picture_full, picture_none, blocked, fetched_at)"
      " SELECT 1, jid, about, verified_name, is_business, business_category, business_address,"
      "  business_email, is_group, group_subject, group_description, group_owner, group_created, participant_count,"
      "  participants, picture, picture_full, picture_none, blocked, fetched_at FROM profiles_v16;"
      "DROP TABLE profiles_v16;"

      "CREATE TABLE statuses_v16 AS SELECT * FROM statuses;"
      "DROP TABLE statuses;"
      "CREATE TABLE statuses ("
      "  account_id INTEGER NOT NULL DEFAULT 1, id TEXT NOT NULL, author_jid TEXT NOT NULL, author_name TEXT NOT NULL DEFAULT '',"
      "  type INTEGER NOT NULL DEFAULT 0, text TEXT, media_ref TEXT, media_path TEXT NOT NULL DEFAULT '',"
      "  thumbnail BLOB, background_argb INTEGER NOT NULL DEFAULT 0, timestamp INTEGER NOT NULL DEFAULT 0,"
      "  from_me INTEGER NOT NULL DEFAULT 0, viewed INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (account_id, id));"
      "INSERT INTO statuses (account_id, id, author_jid, author_name, type, text, media_ref, media_path, thumbnail,"
      "  background_argb, timestamp, from_me, viewed)"
      " SELECT 1, id, author_jid, author_name, type, text, media_ref, media_path, thumbnail,"
      "  background_argb, timestamp, from_me, viewed FROM statuses_v16;"
      "DROP TABLE statuses_v16;"
      "CREATE INDEX idx_statuses_author_ts ON statuses(account_id, author_jid, timestamp);"
      "CREATE INDEX idx_statuses_ts ON statuses(account_id, timestamp);"

      /* What you chose for a person or group, whichever of your accounts they are on: the account
       * that sends to them (0: the primary one) and whether their chats in several accounts show as
       * one (0 follows the setting, 1 always, 2 never). */
      "CREATE TABLE chat_prefs ("
      "  jid TEXT PRIMARY KEY, send_account INTEGER NOT NULL DEFAULT 0, merge INTEGER NOT NULL DEFAULT 0);"

      /* These two keep their own ids, so the account is a plain column. */
      "ALTER TABLE scheduled_messages ADD COLUMN account_id INTEGER NOT NULL DEFAULT 1;"
      "ALTER TABLE automation_log ADD COLUMN account_id INTEGER NOT NULL DEFAULT 1;" },
    { 18,
      /* The words of voice notes, written out by a transcriber: one per message and language
       * ('' when the transcriber did not say which). They go when their message goes, deleted
       * here or for everyone, which the two triggers see to. */
      "CREATE TABLE transcripts ("
      "  account_id INTEGER NOT NULL DEFAULT 1, message_id TEXT NOT NULL, language TEXT NOT NULL DEFAULT '',"
      "  text TEXT NOT NULL, model TEXT NOT NULL DEFAULT '', source TEXT NOT NULL DEFAULT '',"
      "  created_at INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (account_id, message_id, language));"
      "CREATE TRIGGER transcripts_message_removed AFTER DELETE ON messages BEGIN"
      "  DELETE FROM transcripts WHERE account_id = old.account_id AND message_id = old.id;"
      "END;"
      "CREATE TRIGGER transcripts_message_deleted AFTER UPDATE OF deleted ON messages WHEN new.deleted <> 0 BEGIN"
      "  DELETE FROM transcripts WHERE account_id = new.account_id AND message_id = new.id;"
      "END;"
      /* What you chose about transcripts for a person or group: whether theirs show (0 follows the
       * setting, 1 always, 2 never), and whether their voice notes are left untranscribed from now on. */
      "ALTER TABLE chat_prefs ADD COLUMN show_transcripts INTEGER NOT NULL DEFAULT 0;"
      "ALTER TABLE chat_prefs ADD COLUMN transcribe_off INTEGER NOT NULL DEFAULT 0;" },
    { 19,
      /* TL;DR: a long message in a few words, written by an agent's model, one per message. It goes
       * when its message goes or is deleted for everyone, and when the message's text is edited,
       * since it then describes words that are no longer there. */
      "CREATE TABLE summaries ("
      "  account_id INTEGER NOT NULL DEFAULT 1, message_id TEXT NOT NULL, text TEXT NOT NULL,"
      "  model TEXT NOT NULL DEFAULT '', source TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY (account_id, message_id));"
      "CREATE TRIGGER summaries_message_removed AFTER DELETE ON messages BEGIN"
      "  DELETE FROM summaries WHERE account_id = old.account_id AND message_id = old.id;"
      "END;"
      "CREATE TRIGGER summaries_message_changed AFTER UPDATE OF text, deleted ON messages"
      "  WHEN new.deleted <> 0 OR coalesce(new.text, '') <> coalesce(old.text, '') BEGIN"
      "  DELETE FROM summaries WHERE account_id = new.account_id AND message_id = new.id;"
      "END;"
      /* Whether a person or group is in TL;DR mode. */
      "ALTER TABLE chat_prefs ADD COLUMN tldr INTEGER NOT NULL DEFAULT 0;" },
    { 20,
      /* Transcripts written before the transcriber worked out the language properly could be in the wrong
       * one, or a translation. They are dropped, and written again as their voice notes are looked at. */
      "DELETE FROM transcripts;"
      /* The languages a person's or group's voice notes are spoken in ('af,en'), so a transcriber
       * chooses among them; '' leaves it to the setting. */
      "ALTER TABLE chat_prefs ADD COLUMN voice_languages TEXT NOT NULL DEFAULT '';" },
    { 21,
      /* Until this version tawk could hand a voice note to a session still running an older transcriber,
       * which wrote some of them as English translations. Those are dropped too, to be written again. */
      "DELETE FROM transcripts;" },
    { 22,
      /* The owner's chat: the ids of what tawk itself sent into it, so that what is left there is yours. */
      "CREATE TABLE sent_ids ("
      "  account_id INTEGER NOT NULL,"
      "  message_id TEXT NOT NULL,"
      "  kind INTEGER NOT NULL DEFAULT 0,"
      "  ref INTEGER NOT NULL DEFAULT 0,"
      "  at INTEGER NOT NULL DEFAULT 0,"
      "  PRIMARY KEY (account_id, message_id));" },
    { 23,
      /* What agents may do in one chat: 0 as the account says, 1 always ask, 2 read only, 3 hidden from them. */
      "ALTER TABLE chat_prefs ADD COLUMN agent_rule INTEGER NOT NULL DEFAULT 0;" },
};

#define LATEST_VERSION 23

static int user_version(sqlite3 *db) {
    sqlite3_stmt *st = NULL;
    int v = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
        v = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return v;
}

/* Before an existing database is upgraded, a copy of it as it was is kept
 * beside it (tawk.db.pre-v10), once, so a failed or unwanted upgrade can be
 * undone by hand. A new database has nothing to keep. */
static void keep_copy_before_upgrade(sqlite3 *db, const char *path, int current, const Passphrase *key) {
    if (current <= 0 || current >= LATEST_VERSION) return;
    char copy[1100];
    snprintf(copy, sizeof(copy), "%s.pre-v%d", path, current + 1);
    int fd = open(copy, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;                               /* already kept (or cannot be written) */
    close(fd);
    sqlite3 *dest = NULL;
    int ok = sqlite3_open_v2(copy, &dest, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK &&
             sqlite_key_apply(dest, key) == 0;              /* an encrypted database keeps an encrypted copy */
    sqlite3_backup *b = ok ? sqlite3_backup_init(dest, "main", db, "main") : NULL;
    ok = b && sqlite3_backup_step(b, -1) == SQLITE_DONE;
    if (b) sqlite3_backup_finish(b);
    sqlite3_close(dest);
    if (ok) LOG_INFO("kept a copy of the database before upgrading it: %s", copy);
    else { LOG_WARN("could not keep a copy of the database before upgrading it"); unlink(copy); }
}

static int migrate(sqlite3 *db) {
    int current = user_version(db);
    const size_t count = sizeof(MIGRATIONS) / sizeof(MIGRATIONS[0]);
    for (size_t i = 0; i < count;) {
        int version = MIGRATIONS[i].version;
        size_t end = i;
        while (end < count && MIGRATIONS[end].version == version) end++;
        if (version <= current) { i = end; continue; }
        char *err = NULL;
        char pragma[48];
        snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d;", version);
        int ok = sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) == SQLITE_OK;
        for (size_t step = i; ok && step < end; step++) ok = sqlite3_exec(db, MIGRATIONS[step].sql, NULL, NULL, &err) == SQLITE_OK;
        ok = ok && sqlite3_exec(db, pragma, NULL, NULL, &err) == SQLITE_OK && sqlite3_exec(db, "COMMIT", NULL, NULL, &err) == SQLITE_OK;
        if (!ok) {
            LOG_ERROR("database migration to version %d failed: %s", version, err ? err : "?");
            sqlite3_free(err);
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
            return -1;
        }
        LOG_INFO("database upgraded to version %d", version);
        i = end;
    }
    return 0;
}

/* Full-text search over message text, kept in sync by triggers. */
static const char SEARCH_INDEX[] =
    "CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5("
    "  text, content='messages', content_rowid='rowid', tokenize='unicode61 remove_diacritics 2');"
    "CREATE TRIGGER IF NOT EXISTS messages_fts_ai AFTER INSERT ON messages BEGIN"
    "  INSERT INTO messages_fts(rowid, text) VALUES (new.rowid, coalesce(new.text, ''));"
    "END;"
    "CREATE TRIGGER IF NOT EXISTS messages_fts_ad AFTER DELETE ON messages BEGIN"
    "  INSERT INTO messages_fts(messages_fts, rowid, text) VALUES ('delete', old.rowid, coalesce(old.text, ''));"
    "END;"
    "CREATE TRIGGER IF NOT EXISTS messages_fts_au AFTER UPDATE OF text ON messages BEGIN"
    "  INSERT INTO messages_fts(messages_fts, rowid, text) VALUES ('delete', old.rowid, coalesce(old.text, ''));"
    "  INSERT INTO messages_fts(rowid, text) VALUES (new.rowid, coalesce(new.text, ''));"
    "END;";

static int has_fts5(sqlite3 *db) {
    int ok = sqlite3_exec(db, "CREATE VIRTUAL TABLE temp.tawk_fts5_probe USING fts5(x);", NULL, NULL, NULL) == SQLITE_OK;
    if (ok) sqlite3_exec(db, "DROP TABLE temp.tawk_fts5_probe;", NULL, NULL, NULL);
    return ok;
}

static int has_trigger(sqlite3 *db, const char *name) {
    sqlite3_stmt *st = NULL;
    int found = 0;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type = 'trigger' AND name = ?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        found = sqlite3_step(st) == SQLITE_ROW;
    }
    sqlite3_finalize(st);
    return found;
}

/* Message search uses an FTS5 index where SQLite has FTS5. Some builds
 * (Ubuntu's SQLCipher) do not: there the index's triggers are dropped, so
 * saving messages keeps working, and search falls back to plain matching.
 * When FTS5 is back, the index is brought up to date again. */
static void ensure_search_index(sqlite3 *db) {
    if (!has_fts5(db)) {
        sqlite3_exec(db, "DROP TRIGGER IF EXISTS messages_fts_ai; DROP TRIGGER IF EXISTS messages_fts_ad;"
                         "DROP TRIGGER IF EXISTS messages_fts_au;", NULL, NULL, NULL);
        LOG_INFO("this SQLite has no FTS5: message search matches text without an index");
        return;
    }
    int stale = !has_trigger(db, "messages_fts_ai");      /* new, or messages saved without the index */
    char *err = NULL;
    if (sqlite3_exec(db, SEARCH_INDEX, NULL, NULL, &err) != SQLITE_OK) {
        LOG_WARN("search index: %s", err ? err : "?");
        sqlite3_free(err);
        return;
    }
    if (stale) sqlite3_exec(db, "INSERT INTO messages_fts(messages_fts) VALUES ('rebuild');", NULL, NULL, NULL);
}

sqlite3 *sqlite_database_open(const char *path, const Passphrase *key) {
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        LOG_ERROR("cannot create database %s", path);
        return NULL;
    }
    fchmod(fd, 0600);
    close(fd);

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK) {
        LOG_ERROR("cannot open database: %s", db ? sqlite3_errmsg(db) : "out of memory");
        sqlite3_close(db);
        return NULL;
    }
    if (sqlite_key_apply(db, key) != 0 || sqlite_key_verify(db) != 0) {
        LOG_ERROR("cannot read the database: the passphrase is wrong or the file is damaged");
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 2000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

    char *err = NULL;
    keep_copy_before_upgrade(db, path, user_version(db), key);
    /* The first tables are made only in a new file. Later versions reshape
     * them, and making them again here would bring an old index back. */
    int fresh = user_version(db) == 0;
    if ((fresh && sqlite3_exec(db, BASE_SCHEMA, NULL, NULL, &err) != SQLITE_OK) || migrate(db) != 0) {
        LOG_ERROR("database schema setup failed: %s", err ? err : "see above");
        sqlite3_free(err);
        sqlite3_close(db);
        return NULL;
    }
    ensure_search_index(db);
    return db;
}

void sqlite_database_close(sqlite3 *db) {
    if (db) sqlite3_close(db);
}
