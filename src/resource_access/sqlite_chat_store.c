#include "resource_access/sqlite_chat_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define COLUMNS "jid, name, preview, last_ts, unread, is_group, is_muted, is_pinned, is_archived, is_locked, muted_until, tone, " \
                "substr(replace(draft, char(10), ' '), 1, 200), theme, soft_locked, unread_mention"

static SqliteAccountScope *scope_of(IChatStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(IChatStore *self) { return scope_of(self)->db; }

static int finish(sqlite3 *db, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static void read_row(sqlite3_stmt *st, Chat *c) {
    memset(c, 0, sizeof(*c));
    str_copy(c->jid, sizeof(c->jid), (const char *)sqlite3_column_text(st, 0));
    str_copy(c->name, sizeof(c->name), (const char *)sqlite3_column_text(st, 1));
    str_copy(c->preview, sizeof(c->preview), (const char *)sqlite3_column_text(st, 2));
    c->last_ts = sqlite3_column_int64(st, 3);
    c->unread = sqlite3_column_int(st, 4);
    c->is_group = sqlite3_column_int(st, 5);
    c->is_pinned = sqlite3_column_int(st, 7);
    c->is_archived = sqlite3_column_int(st, 8);
    c->is_locked = sqlite3_column_int(st, 9);
    c->muted_until = sqlite3_column_int64(st, 10);
    c->is_muted = c->muted_until == -1 || c->muted_until > (int64_t)time(NULL);
    str_copy(c->tone, sizeof(c->tone), (const char *)sqlite3_column_text(st, 11));
    str_copy(c->theme, sizeof(c->theme), (const char *)sqlite3_column_text(st, 13));
    c->soft_locked = sqlite3_column_int(st, 14);
    c->unread_mention = sqlite3_column_int(st, 15);
    const char *draft = (const char *)sqlite3_column_text(st, 12);
    c->has_draft = draft && draft[0];
    if (c->has_draft) snprintf(c->preview, sizeof(c->preview), "\xE2\x9C\x8F Draft: %s", draft);
}

static int store_upsert(IChatStore *self, const Chat *c) {
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO chats (jid, name, preview, last_ts, unread, is_group, is_archived, is_locked, account_id) "
        "VALUES (?,?,?,?,MAX(?,0),?,MAX(?,0),MAX(?,0),{acct}) "
        "ON CONFLICT(account_id, jid) DO UPDATE SET "
        " name = CASE WHEN excluded.name <> '' THEN excluded.name ELSE name END,"
        " preview = CASE WHEN excluded.preview <> '' AND excluded.last_ts >= last_ts THEN excluded.preview ELSE preview END,"
        " last_ts = MAX(last_ts, excluded.last_ts),"
        " unread = CASE WHEN ?11 >= 0 THEN excluded.unread ELSE unread END,"
        " is_group = excluded.is_group,"
        " is_archived = CASE WHEN ?9 >= 0 THEN ?9 ELSE is_archived END,"
        " is_locked = CASE WHEN ?10 >= 0 THEN ?10 ELSE is_locked END";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, c->jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, c->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, c->preview, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, c->last_ts);
    sqlite3_bind_int(st, 5, c->unread);
    sqlite3_bind_int(st, 6, c->is_group);
    sqlite3_bind_int(st, 7, c->is_archived);
    sqlite3_bind_int(st, 8, c->is_locked);
    sqlite3_bind_int(st, 11, c->unread);
    sqlite3_bind_int(st, 9, c->is_archived);
    sqlite3_bind_int(st, 10, c->is_locked);
    return finish(db_of(self), st);
}

static int store_touch(IChatStore *self, const char *jid, int64_t ts, const char *preview) {
    Chat c;
    chat_init(&c, jid);
    c.last_ts = ts;
    c.unread = -1;
    str_copy(c.preview, sizeof(c.preview), preview);
    return store_upsert(self, &c);
}

static int store_get_all(IChatStore *self, Chat **out, int *count) {
    *out = NULL;
    *count = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT " COLUMNS " FROM chats WHERE account_id = {acct}", &st) != SQLITE_OK) return -1;
    int cap = 64, n = 0;
    Chat *items = malloc((size_t)cap * sizeof(Chat));
    while (items && sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            Chat *grown = realloc(items, (size_t)cap * 2 * sizeof(Chat));
            if (!grown) break;
            items = grown;
            cap *= 2;
        }
        read_row(st, &items[n++]);
    }
    sqlite3_finalize(st);
    if (!items) return -1;
    *out = items;
    *count = n;
    return 0;
}

static int store_get(IChatStore *self, const char *jid, Chat *out) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT " COLUMNS " FROM chats WHERE account_id = {acct} AND jid = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) read_row(st, out);
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

static int update_int(IChatStore *self, const char *sql, int value, const char *jid) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, value);
    sqlite3_bind_text(st, 2, jid, -1, SQLITE_TRANSIENT);
    return finish(db_of(self), st);
}

static int store_set_unread(IChatStore *self, const char *jid, int unread) {
    return update_int(self, "UPDATE chats SET unread = MAX(?1, 0), unread_mention = CASE WHEN ?1 > 0 THEN unread_mention ELSE 0 END"
                            " WHERE account_id = {acct} AND jid = ?2", unread, jid);
}

static int store_mark_mention(IChatStore *self, const char *jid) {
    return update_int(self, "UPDATE chats SET unread_mention = ? WHERE account_id = {acct} AND jid = ?", 1, jid);
}

static int store_add_unread(IChatStore *self, const char *jid, int delta) {
    return update_int(self, "UPDATE chats SET unread = MAX(unread + ?, 0) WHERE account_id = {acct} AND jid = ?", delta, jid);
}

static int store_set_muted(IChatStore *self, const char *jid, int muted) {
    return update_int(self, "UPDATE chats SET muted_until = CASE WHEN ? THEN -1 ELSE 0 END WHERE account_id = {acct} AND jid = ?", muted ? 1 : 0, jid);
}

static int store_set_muted_until(IChatStore *self, const char *jid, int64_t until) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "UPDATE chats SET muted_until = ? WHERE account_id = {acct} AND jid = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, until);
    sqlite3_bind_text(st, 2, jid, -1, SQLITE_TRANSIENT);
    return finish(db_of(self), st);
}

static int update_text(IChatStore *self, const char *sql, const char *value, const char *jid) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, value ? value : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, jid, -1, SQLITE_TRANSIENT);
    return finish(db_of(self), st);
}

static int store_set_tone(IChatStore *self, const char *jid, const char *tone) {
    return update_text(self, "UPDATE chats SET tone = ? WHERE account_id = {acct} AND jid = ?", tone, jid);
}

static int store_set_theme(IChatStore *self, const char *jid, const char *theme) {
    return update_text(self, "UPDATE chats SET theme = ? WHERE account_id = {acct} AND jid = ?", theme, jid);
}

static int store_set_draft(IChatStore *self, const char *jid, const char *draft) {
    return update_text(self, "UPDATE chats SET draft = ? WHERE account_id = {acct} AND jid = ?", draft, jid);
}

static int store_remove(IChatStore *self, const char *jid) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "DELETE FROM chats WHERE account_id = {acct} AND jid = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    return finish(db_of(self), st);
}

static int store_set_soft_locked(IChatStore *self, const char *jid, int locked) {
    return update_int(self, "UPDATE chats SET soft_locked = ? WHERE account_id = {acct} AND jid = ?", locked ? 1 : 0, jid);
}

static int store_set_archived(IChatStore *self, const char *jid, int archived) {
    return update_int(self, "UPDATE chats SET is_archived = ? WHERE account_id = {acct} AND jid = ?", archived ? 1 : 0, jid);
}

static char *store_get_draft(IChatStore *self, const char *jid) {
    sqlite3_stmt *st = NULL;
    char *out = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT draft FROM chats WHERE account_id = {acct} AND jid = ?", &st) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) out = str_dup((const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
    }
    return out ? out : str_dup("");
}

static int store_set_pinned(IChatStore *self, const char *jid, int pinned) {
    return update_int(self, "UPDATE chats SET is_pinned = ? WHERE account_id = {acct} AND jid = ?", pinned ? 1 : 0, jid);
}

static int store_merge(IChatStore *self, const char *from, const char *to) {
    static const char *const SQL[] = {
        "INSERT INTO chats (account_id, jid, name, preview, last_ts, unread, is_group, is_muted, is_pinned, is_archived, is_locked, muted_until, tone, draft, theme) "
        "SELECT {acct}, ?2, name, preview, last_ts, unread, is_group, is_muted, is_pinned, is_archived, is_locked, muted_until, tone, draft, theme "
        "FROM chats WHERE account_id = {acct} AND jid = ?1 "
        "ON CONFLICT(account_id, jid) DO UPDATE SET "
        " name = CASE WHEN chats.name = '' THEN excluded.name ELSE chats.name END,"
        " preview = CASE WHEN excluded.last_ts > chats.last_ts THEN excluded.preview ELSE chats.preview END,"
        " last_ts = MAX(chats.last_ts, excluded.last_ts),"
        " unread = chats.unread + excluded.unread,"
        " is_muted = MAX(chats.is_muted, excluded.is_muted),"
        " is_pinned = MAX(chats.is_pinned, excluded.is_pinned),"
        " muted_until = CASE WHEN chats.muted_until = 0 THEN excluded.muted_until ELSE chats.muted_until END,"
        " tone = CASE WHEN chats.tone = '' THEN excluded.tone ELSE chats.tone END,"
        " draft = CASE WHEN chats.draft = '' THEN excluded.draft ELSE chats.draft END,"
        " theme = CASE WHEN chats.theme = '' THEN excluded.theme ELSE chats.theme END",
        "DELETE FROM chats WHERE account_id = {acct} AND jid = ?1",
    };
    for (int i = 0; i < 2; i++) {
        sqlite3_stmt *st = NULL;
        if (sqlite_account_scope_prepare(scope_of(self), SQL[i], &st) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, from, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, to, -1, SQLITE_TRANSIENT);
        if (finish(db_of(self), st) != 0) return -1;
    }
    return 0;
}

static void store_destroy(IChatStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IChatStore *sqlite_chat_store_create(sqlite3 *db, AccountId account) {
    IChatStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->upsert = store_upsert;
    s->touch = store_touch;
    s->get_all = store_get_all;
    s->get = store_get;
    s->set_unread = store_set_unread;
    s->add_unread = store_add_unread;
    s->mark_mention = store_mark_mention;
    s->set_muted = store_set_muted;
    s->set_pinned = store_set_pinned;
    s->merge = store_merge;
    s->set_muted_until = store_set_muted_until;
    s->set_tone = store_set_tone;
    s->set_archived = store_set_archived;
    s->set_soft_locked = store_set_soft_locked;
    s->remove = store_remove;
    s->set_draft = store_set_draft;
    s->set_theme = store_set_theme;
    s->get_draft = store_get_draft;
    s->destroy = store_destroy;
    return s;
}
