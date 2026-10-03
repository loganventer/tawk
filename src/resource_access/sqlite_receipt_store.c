#include "resource_access/sqlite_receipt_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>

static SqliteAccountScope *scope_of(IReceiptStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(IReceiptStore *self) { return scope_of(self)->db; }

/* Keeps the earliest non-zero time of each kind. */
#define EARLIEST(col) col " = CASE WHEN excluded." col " = 0 THEN " col " WHEN " col " = 0 THEN excluded." col \
                      " ELSE MIN(" col ", excluded." col ") END"

static int receipt_put(IReceiptStore *self, const char *message_id, const char *jid, ReceiptKind kind, int64_t at) {
    if (!message_id || !message_id[0] || !jid || !jid[0] || kind == RECEIPT_NONE || at <= 0) return -1;
    int64_t delivered = at, read = kind >= RECEIPT_READ ? at : 0, played = kind == RECEIPT_PLAYED ? at : 0;
    static const char *const SQL =
        "INSERT INTO message_receipts (message_id, jid, delivered_at, read_at, played_at, account_id) VALUES (?, ?, ?, ?, ?, {acct}) "
        "ON CONFLICT(account_id, message_id, jid) DO UPDATE SET "
        EARLIEST("delivered_at") ", " EARLIEST("read_at") ", " EARLIEST("played_at");
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), SQL, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, message_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, delivered);
    sqlite3_bind_int64(st, 4, read);
    sqlite3_bind_int64(st, 5, played);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db_of(self)));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int receipt_list(IReceiptStore *self, const char *message_id, Receipt *out, int max) {
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT jid, delivered_at, read_at, played_at FROM message_receipts WHERE account_id = {acct} AND message_id = ? "
                      "ORDER BY read_at = 0, read_at, delivered_at";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, message_id, -1, SQLITE_TRANSIENT);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        Receipt *r = &out[n++];
        str_copy(r->jid, sizeof(r->jid), (const char *)sqlite3_column_text(st, 0));
        r->name[0] = '\0';
        r->delivered_at = sqlite3_column_int64(st, 1);
        r->read_at = sqlite3_column_int64(st, 2);
        r->played_at = sqlite3_column_int64(st, 3);
    }
    sqlite3_finalize(st);
    return n;
}

static int receipt_reassign_jid(IReceiptStore *self, const char *from, const char *to) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "UPDATE OR REPLACE message_receipts SET jid = ?2 WHERE account_id = {acct} AND jid = ?1", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, from, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, to, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static void receipt_destroy(IReceiptStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IReceiptStore *sqlite_receipt_store_create(sqlite3 *db, AccountId account) {
    IReceiptStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->put = receipt_put;
    s->list = receipt_list;
    s->reassign_jid = receipt_reassign_jid;
    s->destroy = receipt_destroy;
    return s;
}
