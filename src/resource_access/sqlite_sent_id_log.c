#include "resource_access/sqlite_sent_id_log.h"
#include "utilities/log.h"

#include <stdlib.h>

static sqlite3 *db_of(ISentIdLog *self) { return (sqlite3 *)self->ctx; }

static int finish(ISentIdLog *self, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db_of(self)));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int sent_note(ISentIdLog *self, AccountId account, const char *message_id, SentKind kind, int ref, int64_t at) {
    if (!message_id || !message_id[0]) return -1;
    sqlite3_stmt *st = NULL;
    static const char *const SQL =
        "INSERT INTO sent_ids (account_id, message_id, kind, ref, at) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(account_id, message_id) DO UPDATE SET kind = excluded.kind, ref = excluded.ref, at = excluded.at";
    if (sqlite3_prepare_v2(db_of(self), SQL, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, account);
    sqlite3_bind_text(st, 2, message_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)kind);
    sqlite3_bind_int(st, 4, ref);
    sqlite3_bind_int64(st, 5, at);
    return finish(self, st);
}

static int sent_find(ISentIdLog *self, AccountId account, const char *message_id, SentKind *kind, int *ref) {
    if (!message_id || !message_id[0]) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), "SELECT kind, ref FROM sent_ids WHERE account_id = ? AND message_id = ?", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, account);
    sqlite3_bind_text(st, 2, message_id, -1, SQLITE_TRANSIENT);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) {
        if (kind) *kind = (SentKind)sqlite3_column_int(st, 0);
        if (ref) *ref = sqlite3_column_int(st, 1);
    }
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

static int sent_prune(ISentIdLog *self, int64_t before) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), "DELETE FROM sent_ids WHERE at < ?", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, before);
    return finish(self, st);
}

static void sent_destroy(ISentIdLog *self) { free(self); }

ISentIdLog *sqlite_sent_id_log_create(sqlite3 *db) {
    ISentIdLog *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx = db;
    s->note = sent_note;
    s->find = sent_find;
    s->prune = sent_prune;
    s->destroy = sent_destroy;
    return s;
}
