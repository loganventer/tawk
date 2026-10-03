#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COLUMNS "id, chat_jid, sender_jid, sender_name, text, media_ref, media_path, type, status, ts, from_me, duration, " \
                "quoted_id, quoted_sender, quoted_text, thumbnail, edited, deleted, " \
                "mentions, mentions_me, forwarded, link_url, link_title, link_desc, quoted_status"

static SqliteAccountScope *scope_of(IMessageStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(IMessageStore *self) { return scope_of(self)->db; }

static int exec_step(sqlite3 *db, sqlite3_stmt *stmt) {
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) LOG_WARN("sqlite: %s", sqlite3_errmsg(db));
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE || rc == SQLITE_ROW) ? 0 : -1;
}

static void read_row(sqlite3_stmt *st, Message *m) {
    message_init(m);
    str_copy(m->id, sizeof(m->id), (const char *)sqlite3_column_text(st, 0));
    str_copy(m->chat_jid, sizeof(m->chat_jid), (const char *)sqlite3_column_text(st, 1));
    str_copy(m->sender_jid, sizeof(m->sender_jid), (const char *)sqlite3_column_text(st, 2));
    str_copy(m->sender_name, sizeof(m->sender_name), (const char *)sqlite3_column_text(st, 3));
    message_set_text(m, (const char *)sqlite3_column_text(st, 4));
    message_set_media_ref(m, (const char *)sqlite3_column_text(st, 5));
    str_copy(m->media_path, sizeof(m->media_path), (const char *)sqlite3_column_text(st, 6));
    m->type = (MessageType)sqlite3_column_int(st, 7);
    m->status = (MessageStatus)sqlite3_column_int(st, 8);
    m->timestamp = sqlite3_column_int64(st, 9);
    m->from_me = sqlite3_column_int(st, 10);
    m->duration_s = sqlite3_column_int(st, 11);
    str_copy(m->quoted_id, sizeof(m->quoted_id), (const char *)sqlite3_column_text(st, 12));
    str_copy(m->quoted_sender, sizeof(m->quoted_sender), (const char *)sqlite3_column_text(st, 13));
    message_set_quoted_text(m, (const char *)sqlite3_column_text(st, 14));
    message_set_thumbnail(m, sqlite3_column_blob(st, 15), sqlite3_column_bytes(st, 15));
    m->edited = sqlite3_column_int(st, 16);
    m->deleted = sqlite3_column_int(st, 17);
    message_set_mentions(m, (const char *)sqlite3_column_text(st, 18));
    m->mentions_me = sqlite3_column_int(st, 19);
    m->forwarded = sqlite3_column_int(st, 20);
    message_set_link(m, link_preview_create((const char *)sqlite3_column_text(st, 21), (const char *)sqlite3_column_text(st, 22),
                                            (const char *)sqlite3_column_text(st, 23)));
    m->quoted_status = sqlite3_column_int(st, 24);
    if (m->type < 0 || m->type >= MESSAGE_TYPE_COUNT) m->type = MESSAGE_TYPE_OTHER;
}

static int store_save(IMessageStore *self, const Message *m) {
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO messages (" COLUMNS ", account_id) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,{acct}) "
        "ON CONFLICT(account_id, id) DO UPDATE SET "
        " text = COALESCE(excluded.text, text),"
        " media_ref = COALESCE(excluded.media_ref, media_ref),"
        " media_path = CASE WHEN excluded.media_path <> '' THEN excluded.media_path ELSE media_path END,"
        " sender_name = CASE WHEN excluded.sender_name <> '' THEN excluded.sender_name ELSE sender_name END,"
        /* Failed (4) is not "further" than read: any progress replaces it,
         * and a failure only sticks while nothing went through yet. */
        " status = CASE WHEN excluded.status = 4 THEN (CASE WHEN status IN (0, 4) THEN 4 ELSE status END)"
        "               WHEN status = 4 THEN excluded.status ELSE MAX(status, excluded.status) END,"
        " ts = CASE WHEN ts > 0 THEN ts ELSE excluded.ts END,"
        " duration = MAX(duration, excluded.duration),"
        " quoted_id = CASE WHEN excluded.quoted_id <> '' THEN excluded.quoted_id ELSE quoted_id END,"
        " quoted_sender = CASE WHEN excluded.quoted_sender <> '' THEN excluded.quoted_sender ELSE quoted_sender END,"
        " quoted_text = COALESCE(excluded.quoted_text, quoted_text),"
        " thumbnail = COALESCE(excluded.thumbnail, thumbnail),"
        " mentions = COALESCE(excluded.mentions, mentions),"
        " mentions_me = MAX(mentions_me, excluded.mentions_me),"
        " forwarded = MAX(forwarded, excluded.forwarded),"
        " link_url = COALESCE(excluded.link_url, link_url),"
        " link_title = COALESCE(excluded.link_title, link_title),"
        " link_desc = COALESCE(excluded.link_desc, link_desc),"
        " quoted_status = MAX(quoted_status, excluded.quoted_status)";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, m->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, m->chat_jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, m->sender_jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, m->sender_name, -1, SQLITE_TRANSIENT);
    if (m->text) sqlite3_bind_text(st, 5, m->text, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, 5);
    if (m->media_ref) sqlite3_bind_text(st, 6, m->media_ref, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, 6);
    sqlite3_bind_text(st, 7, m->media_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 8, m->type);
    sqlite3_bind_int(st, 9, m->status);
    sqlite3_bind_int64(st, 10, m->timestamp);
    sqlite3_bind_int(st, 11, m->from_me);
    sqlite3_bind_int(st, 12, m->duration_s);
    sqlite3_bind_text(st, 13, m->quoted_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 14, m->quoted_sender, -1, SQLITE_TRANSIENT);
    if (m->quoted_text) sqlite3_bind_text(st, 15, m->quoted_text, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, 15);
    if (m->thumbnail && m->thumbnail_len > 0) sqlite3_bind_blob(st, 16, m->thumbnail, m->thumbnail_len, SQLITE_TRANSIENT);
    else sqlite3_bind_null(st, 16);
    sqlite3_bind_int(st, 17, m->edited);
    sqlite3_bind_int(st, 18, m->deleted);
    if (m->mentions) sqlite3_bind_text(st, 19, m->mentions, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, 19);
    sqlite3_bind_int(st, 20, m->mentions_me);
    sqlite3_bind_int(st, 21, m->forwarded);
    for (int i = 0; i < 3; i++) {
        const char *v = !m->link ? NULL : i == 0 ? m->link->url : i == 1 ? m->link->title : m->link->description;
        if (v) sqlite3_bind_text(st, 22 + i, v, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, 22 + i);
    }
    sqlite3_bind_int(st, 25, m->quoted_status);
    return exec_step(db_of(self), st);
}

/* Up to `limit` messages of a chat, newest first in the query and handed
 * back oldest first; only those older than `before` when it is not 0, and
 * without the newest `skip` of them. */
static int select_page(IMessageStore *self, const char *jid, int64_t before, int skip, int limit, Message **out, int *count) {
    *out = NULL;
    *count = 0;
    sqlite3_stmt *st = NULL;
    const char *sql = before > 0
        ? "SELECT " COLUMNS " FROM messages WHERE account_id = {acct} AND chat_jid = ? AND ts < ? ORDER BY ts DESC, rowid DESC LIMIT ? OFFSET ?"
        : "SELECT " COLUMNS " FROM messages WHERE account_id = {acct} AND chat_jid = ? ORDER BY ts DESC, rowid DESC LIMIT ? OFFSET ?";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    int arg = 1;
    sqlite3_bind_text(st, arg++, jid, -1, SQLITE_TRANSIENT);
    if (before > 0) sqlite3_bind_int64(st, arg++, before);
    sqlite3_bind_int(st, arg++, limit);
    sqlite3_bind_int(st, arg, skip > 0 ? skip : 0);
    /* Grows as rows arrive, so a large limit (an export) costs only what the chat holds. */
    int cap = limit > 0 && limit < 256 ? limit : 256;
    Message *items = calloc((size_t)cap, sizeof(Message));
    if (!items) { sqlite3_finalize(st); return -1; }
    int n = 0;
    while (n < limit && sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            Message *bigger = realloc(items, (size_t)cap * 2 * sizeof(Message));
            if (!bigger) break;
            memset(bigger + cap, 0, (size_t)cap * sizeof(Message));
            items = bigger;
            cap *= 2;
        }
        read_row(st, &items[n++]);
    }
    sqlite3_finalize(st);
    for (int i = 0; i < n / 2; i++) {          /* oldest first */
        Message tmp = items[i];
        items[i] = items[n - 1 - i];
        items[n - 1 - i] = tmp;
    }
    *out = items;
    *count = n;
    return 0;
}

static int store_recent(IMessageStore *self, const char *jid, int limit, Message **out, int *count) {
    return select_page(self, jid, 0, 0, limit, out, count);
}

static int store_slice(IMessageStore *self, const char *jid, int skip, int limit, Message **out, int *count) {
    return select_page(self, jid, 0, skip, limit, out, count);
}

static int store_before(IMessageStore *self, const char *jid, int64_t before, int limit, Message **out, int *count) {
    return select_page(self, jid, before, 0, limit, out, count);
}

static int store_get(IMessageStore *self, const char *id, Message *out) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT " COLUMNS " FROM messages WHERE account_id = {acct} AND id = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) read_row(st, out);
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

static int store_update_status(IMessageStore *self, const char *id, MessageStatus status) {
    sqlite3_stmt *st = NULL;
    /* Ticks only move forward, except that a retry (pending) or any
     * progress replaces a failure. */
    const char *sql = status == MESSAGE_STATUS_FAILED
        ? "UPDATE messages SET status = ? WHERE account_id = {acct} AND id = ?"
        : "UPDATE messages SET status = CASE WHEN status = 4 THEN ?1 ELSE MAX(status, ?1) END WHERE account_id = {acct} AND id = ?2";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, status);
    sqlite3_bind_text(st, 2, id, -1, SQLITE_TRANSIENT);
    return exec_step(db_of(self), st);
}

static int store_set_media_path(IMessageStore *self, const char *id, const char *path) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "UPDATE messages SET media_path = ? WHERE account_id = {acct} AND id = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, id, -1, SQLITE_TRANSIENT);
    return exec_step(db_of(self), st);
}

/* Turns free text into a safe FTS5 query: every word becomes a quoted
 * prefix term, so user input can never use FTS operators or syntax. */
static int build_fts_query(const char *input, char *out, size_t size) {
    size_t used = 0;
    int terms = 0;
    const char *p = input;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (used + 4 >= size) break;
        if (terms) out[used++] = ' ';
        out[used++] = '"';
        while (*p && *p != ' ' && *p != '\t' && used + 4 < size) {
            if (*p == '"') out[used++] = '"';          /* "" escapes a quote inside a string */
            out[used++] = *p++;
        }
        out[used++] = '"';
        out[used++] = '*';
        terms++;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    out[used] = '\0';
    return terms;
}

/* Runs a search statement whose last parameter is the row limit. */
static int collect(sqlite3_stmt *st, int limit, Message **out, int *count) {
    Message *items = calloc((size_t)(limit > 0 ? limit : 1), sizeof(Message));
    int n = 0;
    while (items && n < limit && sqlite3_step(st) == SQLITE_ROW) read_row(st, &items[n++]);
    sqlite3_finalize(st);
    *out = items;
    *count = n;
    return items ? 0 : -1;
}

#define PLAIN_TERMS 8

/* Without an FTS5 index (some SQLite builds lack FTS5): every word must
 * appear in the text. Slower on long histories, same results for words. */
static int search_plain(IMessageStore *self, const char *query, int limit, Message **out, int *count) {
    char words[PLAIN_TERMS][130];
    int n = 0;
    for (const char *p = query; *p && n < PLAIN_TERMS;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        size_t used = 0;
        words[n][used++] = '%';
        while (*p && *p != ' ' && *p != '\t') {
            if (used + 3 < sizeof(words[n])) {
                if (*p == '%' || *p == '_' || *p == '\\') words[n][used++] = '\\';   /* literal, not a wildcard */
                words[n][used++] = *p;
            }
            p++;
        }
        words[n][used++] = '%';
        words[n][used] = '\0';
        n++;
    }
    if (n == 0) return 0;
    char sql[1024];
    int len = snprintf(sql, sizeof(sql), "SELECT " COLUMNS " FROM messages WHERE account_id = {acct} AND ");
    for (int i = 0; i < n; i++) len += snprintf(sql + len, sizeof(sql) - (size_t)len, "%stext LIKE ? ESCAPE '\\'", i ? " AND " : "");
    snprintf(sql + len, sizeof(sql) - (size_t)len, " ORDER BY ts DESC LIMIT ?");
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) {
        LOG_WARN("search unavailable: %s", sqlite3_errmsg(db_of(self)));
        return -1;
    }
    for (int i = 0; i < n; i++) sqlite3_bind_text(st, i + 1, words[i], -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, n + 1, limit);
    return collect(st, limit, out, count);
}

static int search_index_ready(sqlite3 *db) {
    sqlite3_stmt *st = NULL;
    int ready = 0;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type = 'trigger' AND name = 'messages_fts_ai'", -1, &st, NULL) == SQLITE_OK)
        ready = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return ready;
}

static int store_search(IMessageStore *self, const char *query, int limit, Message **out, int *count) {
    *out = NULL;
    *count = 0;
    char fts[512];
    if (!query || build_fts_query(query, fts, sizeof(fts)) == 0) return 0;
    if (!search_index_ready(db_of(self))) return search_plain(self, query, limit, out, count);
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT " COLUMNS " FROM messages WHERE account_id = {acct} AND rowid IN "
        "(SELECT rowid FROM messages_fts WHERE messages_fts MATCH ? ORDER BY rank LIMIT 500) "
        "ORDER BY ts DESC LIMIT ?";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return search_plain(self, query, limit, out, count);
    sqlite3_bind_text(st, 1, fts, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, limit);
    return collect(st, limit, out, count);
}

static int store_edit_text(IMessageStore *self, const char *id, const char *text, int deleted) {
    sqlite3_stmt *st = NULL;
    const char *sql = "UPDATE messages SET text = ?, edited = CASE WHEN ? THEN edited ELSE 1 END, deleted = ? WHERE account_id = {acct} AND id = ?";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    if (deleted || !text) sqlite3_bind_null(st, 1); else sqlite3_bind_text(st, 1, text, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, deleted);
    sqlite3_bind_int(st, 3, deleted);
    sqlite3_bind_text(st, 4, id, -1, SQLITE_TRANSIENT);
    return exec_step(db_of(self), st);
}

static int store_remove(IMessageStore *self, const char *id) {
    static const char *const SQL[] = {
        "DELETE FROM reactions WHERE account_id = {acct} AND message_id = ?",
        "DELETE FROM message_receipts WHERE account_id = {acct} AND message_id = ?",
        "DELETE FROM messages WHERE account_id = {acct} AND id = ?",
    };
    int rc = 0;
    for (size_t i = 0; i < sizeof(SQL) / sizeof(SQL[0]); i++) {
        sqlite3_stmt *st = NULL;
        if (sqlite_account_scope_prepare(scope_of(self), SQL[i], &st) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
        if (exec_step(db_of(self), st) != 0) rc = -1;
    }
    return rc;
}

static int store_remove_chat(IMessageStore *self, const char *jid) {
    static const char *const SQL[] = {
        "DELETE FROM reactions WHERE account_id = {acct} AND message_id IN (SELECT id FROM messages WHERE account_id = {acct} AND chat_jid = ?)",
        "DELETE FROM message_receipts WHERE account_id = {acct} AND message_id IN (SELECT id FROM messages WHERE account_id = {acct} AND chat_jid = ?)",
        "DELETE FROM messages WHERE account_id = {acct} AND chat_jid = ?",
    };
    int rc = 0;
    for (size_t i = 0; i < sizeof(SQL) / sizeof(SQL[0]); i++) {
        sqlite3_stmt *st = NULL;
        if (sqlite_account_scope_prepare(scope_of(self), SQL[i], &st) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
        if (exec_step(db_of(self), st) != 0) rc = -1;
    }
    return rc;
}

static int store_reassign_jid(IMessageStore *self, const char *from, const char *to) {
    static const char *const SQL[] = {
        "UPDATE OR IGNORE messages SET chat_jid = ?2 WHERE account_id = {acct} AND chat_jid = ?1",
        "UPDATE messages SET sender_jid = ?2 WHERE account_id = {acct} AND sender_jid = ?1",
    };
    for (int i = 0; i < 2; i++) {
        sqlite3_stmt *st = NULL;
        if (sqlite_account_scope_prepare(scope_of(self), SQL[i], &st) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, from, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, to, -1, SQLITE_TRANSIENT);
        if (exec_step(db_of(self), st) != 0) return -1;
    }
    return 0;
}

static void store_destroy(IMessageStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IMessageStore *sqlite_message_store_create(sqlite3 *db, AccountId account) {
    IMessageStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->save = store_save;
    s->recent = store_recent;
    s->slice = store_slice;
    s->before = store_before;
    s->get = store_get;
    s->update_status = store_update_status;
    s->set_media_path = store_set_media_path;
    s->reassign_jid = store_reassign_jid;
    s->search = store_search;
    s->edit_text = store_edit_text;
    s->remove = store_remove;
    s->remove_chat = store_remove_chat;
    s->destroy = store_destroy;
    return s;
}
