#include "resource_access/sqlite_scheduled_message_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

#define COLUMNS "id, chat_jid, text, mentions, due_at, created_at, state"

static SqliteAccountScope *scope_of(IScheduledMessageStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(IScheduledMessageStore *self) { return scope_of(self)->db; }

static void read_row(sqlite3_stmt *st, ScheduledMessage *s) {
    scheduled_message_init(s);
    str_copy(s->id, sizeof(s->id), (const char *)sqlite3_column_text(st, 0));
    str_copy(s->chat_jid, sizeof(s->chat_jid), (const char *)sqlite3_column_text(st, 1));
    s->text = str_dup((const char *)sqlite3_column_text(st, 2));
    const char *mentions = (const char *)sqlite3_column_text(st, 3);
    s->mentions = mentions && mentions[0] ? str_dup(mentions) : NULL;
    s->due_at = sqlite3_column_int64(st, 4);
    s->created_at = sqlite3_column_int64(st, 5);
    s->state = (ScheduledState)sqlite3_column_int(st, 6);
}

static int step_done(sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int store_add(IScheduledMessageStore *self, const ScheduledMessage *s) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "INSERT INTO scheduled_messages (" COLUMNS ", account_id) VALUES (?,?,?,?,?,?,?,{acct})", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, s->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, s->chat_jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, s->text ? s->text : "", -1, SQLITE_TRANSIENT);
    if (s->mentions) sqlite3_bind_text(st, 4, s->mentions, -1, SQLITE_TRANSIENT); else sqlite3_bind_null(st, 4);
    sqlite3_bind_int64(st, 5, s->due_at);
    sqlite3_bind_int64(st, 6, s->created_at);
    sqlite3_bind_int(st, 7, s->state);
    return step_done(st);
}

static int store_get(IScheduledMessageStore *self, const char *id, ScheduledMessage *out) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT " COLUMNS " FROM scheduled_messages WHERE account_id = {acct} AND id = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) read_row(st, out);
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

/* Runs an UPDATE or DELETE on one id; 0 only when a row changed. */
static int change_one(IScheduledMessageStore *self, const char *sql, const char *id, int64_t value, int bind_value) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
    if (bind_value) sqlite3_bind_int64(st, 2, value);
    int rc = step_done(st);
    return rc == 0 && sqlite3_changes(db_of(self)) > 0 ? 0 : -1;
}

static int store_set_due(IScheduledMessageStore *self, const char *id, int64_t due) {
    return change_one(self, "UPDATE scheduled_messages SET due_at = ?2 WHERE account_id = {acct} AND id = ?1", id, due, 1);
}

static int store_set_state(IScheduledMessageStore *self, const char *id, ScheduledState state) {
    return change_one(self, "UPDATE scheduled_messages SET state = ?2 WHERE account_id = {acct} AND id = ?1", id, state, 1);
}

static int store_remove(IScheduledMessageStore *self, const char *id) {
    return change_one(self, "DELETE FROM scheduled_messages WHERE account_id = {acct} AND id = ?1", id, 0, 0);
}

static int read_all(sqlite3_stmt *st, ScheduledMessage **out, int *count) {
    int cap = 16, n = 0;
    ScheduledMessage *items = calloc((size_t)cap, sizeof(*items));
    if (!items) { sqlite3_finalize(st); return -1; }
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            ScheduledMessage *bigger = realloc(items, sizeof(*items) * (size_t)cap * 2);
            if (!bigger) break;
            items = bigger;
            cap *= 2;
        }
        read_row(st, &items[n++]);
    }
    sqlite3_finalize(st);
    *out = items;
    *count = n;
    return 0;
}

static int store_list_waiting(IScheduledMessageStore *self, const char *chat, ScheduledMessage **out, int *count) {
    *out = NULL;
    *count = 0;
    sqlite3_stmt *st = NULL;
    const char *sql = chat ? "SELECT " COLUMNS " FROM scheduled_messages WHERE account_id = {acct} AND state = 0 AND chat_jid = ? ORDER BY due_at, rowid"
                           : "SELECT " COLUMNS " FROM scheduled_messages WHERE account_id = {acct} AND state = 0 ORDER BY due_at, rowid";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    if (chat) sqlite3_bind_text(st, 1, chat, -1, SQLITE_TRANSIENT);
    return read_all(st, out, count);
}

static int store_due(IScheduledMessageStore *self, int64_t now, ScheduledMessage **out, int *count) {
    *out = NULL;
    *count = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT " COLUMNS " FROM scheduled_messages WHERE account_id = {acct} AND state = 0 AND due_at <= ? ORDER BY due_at, rowid", &st) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, now);
    return read_all(st, out, count);
}

static int store_reassign_jid(IScheduledMessageStore *self, const char *from, const char *to) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "UPDATE scheduled_messages SET chat_jid = ?2 WHERE account_id = {acct} AND chat_jid = ?1", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, from, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, to, -1, SQLITE_TRANSIENT);
    return step_done(st);
}

static void store_destroy(IScheduledMessageStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IScheduledMessageStore *sqlite_scheduled_message_store_create(sqlite3 *db, AccountId account) {
    IScheduledMessageStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->add = store_add;
    s->get = store_get;
    s->set_due = store_set_due;
    s->set_state = store_set_state;
    s->remove = store_remove;
    s->list_waiting = store_list_waiting;
    s->due = store_due;
    s->reassign_jid = store_reassign_jid;
    s->destroy = store_destroy;
    return s;
}
