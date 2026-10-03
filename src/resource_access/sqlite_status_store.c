#include "resource_access/sqlite_status_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

static SqliteAccountScope *scope_of(IStatusStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(IStatusStore *self) { return scope_of(self)->db; }

static int step_done(IStatusStore *self, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db_of(self)));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static void bind_optional(sqlite3_stmt *st, int index, const char *text) {
    if (text && text[0]) sqlite3_bind_text(st, index, text, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(st, index);
}

static int status_save(IStatusStore *self, const StatusUpdate *u) {
    if (!u || !u->id[0] || !u->author_jid[0]) return -1;
    /* A copy that arrives again (history sync, a second device) only fills
     * in what the first one lacked. */
    static const char *const SQL =
        "INSERT INTO statuses (id, author_jid, author_name, type, text, media_ref, media_path, thumbnail,"
        " background_argb, timestamp, from_me, viewed, account_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, {acct}) "
        "ON CONFLICT(account_id, id) DO UPDATE SET"
        " author_name = CASE WHEN author_name = '' THEN excluded.author_name ELSE author_name END,"
        " media_ref = coalesce(media_ref, excluded.media_ref),"
        " media_path = CASE WHEN media_path = '' THEN excluded.media_path ELSE media_path END,"
        " thumbnail = coalesce(thumbnail, excluded.thumbnail)";
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), SQL, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, u->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, u->author_jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, u->author_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, (int)u->type);
    bind_optional(st, 5, u->text);
    bind_optional(st, 6, u->media_ref);
    sqlite3_bind_text(st, 7, u->media_path, -1, SQLITE_TRANSIENT);
    if (u->thumbnail && u->thumbnail_len > 0) sqlite3_bind_blob(st, 8, u->thumbnail, u->thumbnail_len, SQLITE_TRANSIENT);
    else sqlite3_bind_null(st, 8);
    sqlite3_bind_int64(st, 9, (sqlite3_int64)u->background_argb);
    sqlite3_bind_int64(st, 10, u->timestamp);
    sqlite3_bind_int(st, 11, u->from_me ? 1 : 0);
    sqlite3_bind_int(st, 12, u->viewed ? 1 : 0);
    return step_done(self, st);
}

static int by_id(IStatusStore *self, const char *sql, const char *id) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    return step_done(self, st);
}

static int status_remove(IStatusStore *self, const char *id) {
    return by_id(self, "DELETE FROM statuses WHERE account_id = {acct} AND id = ?", id);
}

static int status_mark_viewed(IStatusStore *self, const char *id) {
    return by_id(self, "UPDATE statuses SET viewed = 1 WHERE account_id = {acct} AND id = ?", id);
}

static int status_set_media_path(IStatusStore *self, const char *id, const char *path) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "UPDATE statuses SET media_path = ?2 WHERE account_id = {acct} AND id = ?1", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, path ? path : "", -1, SQLITE_TRANSIENT);
    return step_done(self, st);
}

#define COLUMNS "id, author_jid, author_name, type, text, media_ref, media_path, thumbnail, background_argb, timestamp, from_me, viewed"

static void read_row(sqlite3_stmt *st, StatusUpdate *u) {
    status_update_init(u);
    str_copy(u->id, sizeof(u->id), (const char *)sqlite3_column_text(st, 0));
    str_copy(u->author_jid, sizeof(u->author_jid), (const char *)sqlite3_column_text(st, 1));
    str_copy(u->author_name, sizeof(u->author_name), (const char *)sqlite3_column_text(st, 2));
    u->type = (MessageType)sqlite3_column_int(st, 3);
    u->text = str_dup((const char *)sqlite3_column_text(st, 4));
    u->media_ref = str_dup((const char *)sqlite3_column_text(st, 5));
    str_copy(u->media_path, sizeof(u->media_path), (const char *)sqlite3_column_text(st, 6));
    int len = sqlite3_column_bytes(st, 7);
    const void *blob = sqlite3_column_blob(st, 7);
    if (blob && len > 0 && (u->thumbnail = malloc((size_t)len)) != NULL) {
        memcpy(u->thumbnail, blob, (size_t)len);
        u->thumbnail_len = len;
    }
    u->background_argb = (uint32_t)sqlite3_column_int64(st, 8);
    u->timestamp = sqlite3_column_int64(st, 9);
    u->from_me = sqlite3_column_int(st, 10);
    u->viewed = sqlite3_column_int(st, 11);
}

static int status_get(IStatusStore *self, const char *id, StatusUpdate *out) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT " COLUMNS " FROM statuses WHERE account_id = {acct} AND id = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) read_row(st, out);
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

static int status_authors(IStatusStore *self, int64_t since, int64_t until, StatusAuthor *out, int max) {
    /* The name comes from the newest row that has one. */
    static const char *const SQL =
        "SELECT author_jid, count(*), sum(viewed = 0 AND from_me = 0), max(timestamp), max(from_me),"
        " (SELECT author_name FROM statuses n WHERE n.account_id = {acct} AND n.author_jid = s.author_jid AND n.author_name != ''"
        "  ORDER BY n.timestamp DESC LIMIT 1) "
        "FROM statuses s WHERE account_id = {acct} AND timestamp > ? AND timestamp <= ? GROUP BY author_jid ORDER BY max(timestamp) DESC";
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), SQL, &st) != SQLITE_OK) return 0;
    sqlite3_bind_int64(st, 1, since);
    sqlite3_bind_int64(st, 2, until);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        StatusAuthor *a = &out[n++];
        memset(a, 0, sizeof(*a));
        str_copy(a->jid, sizeof(a->jid), (const char *)sqlite3_column_text(st, 0));
        a->count = sqlite3_column_int(st, 1);
        a->unviewed = sqlite3_column_int(st, 2);
        a->latest = sqlite3_column_int64(st, 3);
        a->from_me = sqlite3_column_int(st, 4);
        str_copy(a->name, sizeof(a->name), (const char *)sqlite3_column_text(st, 5));
    }
    sqlite3_finalize(st);
    return n;
}

static int status_updates_by(IStatusStore *self, const char *jid, int64_t since, int64_t until, StatusUpdate *out, int max) {
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT " COLUMNS " FROM statuses WHERE account_id = {acct} AND author_jid = ? AND timestamp > ? AND timestamp <= ? ORDER BY timestamp, rowid";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, since);
    sqlite3_bind_int64(st, 3, until);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) read_row(st, &out[n++]);
    sqlite3_finalize(st);
    return n;
}

static int status_prune(IStatusStore *self, int64_t before, void (*on_media)(void *ctx, const char *path), void *ctx) {
    sqlite3_stmt *st = NULL;
    if (on_media &&
        sqlite_account_scope_prepare(scope_of(self), "SELECT media_path FROM statuses WHERE account_id = {acct} AND timestamp <= ? AND media_path != ''", &st) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, before);
        while (sqlite3_step(st) == SQLITE_ROW) on_media(ctx, (const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        st = NULL;
    }
    if (sqlite_account_scope_prepare(scope_of(self), "DELETE FROM statuses WHERE account_id = {acct} AND timestamp <= ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, before);
    if (step_done(self, st) != 0) return -1;
    return sqlite3_changes(db_of(self));
}

static void status_destroy(IStatusStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IStatusStore *sqlite_status_store_create(sqlite3 *db, AccountId account) {
    IStatusStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->save = status_save;
    s->remove = status_remove;
    s->get = status_get;
    s->set_media_path = status_set_media_path;
    s->mark_viewed = status_mark_viewed;
    s->authors = status_authors;
    s->updates_by = status_updates_by;
    s->prune = status_prune;
    s->destroy = status_destroy;
    return s;
}
