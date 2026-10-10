#include "resource_access/sqlite_summary_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>

static SqliteAccountScope *scope_of(ISummaryStore *self) { return (SqliteAccountScope *)self->ctx; }

static int finish(ISummaryStore *self, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(scope_of(self)->db));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int summary_save(ISummaryStore *self, const Summary *s) {
    if (!s || !s->message_id[0] || !s->text) return -1;
    static const char *const SQL =
        "INSERT INTO summaries (message_id, text, model, source, created_at, account_id) VALUES (?, ?, ?, ?, ?, {acct}) "
        "ON CONFLICT(account_id, message_id) DO UPDATE SET "
        "text = excluded.text, model = excluded.model, source = excluded.source, created_at = excluded.created_at";
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), SQL, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, s->message_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, s->text, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, s->model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, s->source, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, s->created_at);
    return finish(self, st);
}

static int summary_find(ISummaryStore *self, const char *message_id, Summary *out) {
    if (!message_id || !message_id[0]) return -1;
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT text, model, source, created_at FROM summaries WHERE account_id = {acct} AND message_id = ?";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, message_id, -1, SQLITE_TRANSIENT);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) {
        summary_init(out);
        str_copy(out->message_id, sizeof(out->message_id), message_id);
        summary_set_text(out, (const char *)sqlite3_column_text(st, 0));
        str_copy(out->model, sizeof(out->model), (const char *)sqlite3_column_text(st, 1));
        str_copy(out->source, sizeof(out->source), (const char *)sqlite3_column_text(st, 2));
        out->created_at = sqlite3_column_int64(st, 3);
    }
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

static int summary_remove(ISummaryStore *self, const char *message_id) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "DELETE FROM summaries WHERE account_id = {acct} AND message_id = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, message_id, -1, SQLITE_TRANSIENT);
    return finish(self, st);
}

static void summary_destroy(ISummaryStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

ISummaryStore *sqlite_summary_store_create(sqlite3 *db, AccountId account) {
    ISummaryStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->save = summary_save;
    s->find = summary_find;
    s->remove = summary_remove;
    s->destroy = summary_destroy;
    return s;
}
