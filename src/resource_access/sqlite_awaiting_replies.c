#include "resource_access/sqlite_awaiting_replies.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/str_util.h"

#include <stdlib.h>

static SqliteAccountScope *scope_of(IAwaitingReplies *self) { return (SqliteAccountScope *)self->ctx; }

static int awaiting_list(IAwaitingReplies *self, int64_t after, int64_t before, char (*jids)[128], int max) {
    /* SQLite gives the other columns of a max() row the values of that row: the newest message of each chat. */
    static const char *const SQL =
        "SELECT chat_jid, max(ts), from_me FROM messages "
        "WHERE account_id = {acct} AND deleted = 0 AND chat_jid NOT LIKE '%@g.us' AND chat_jid NOT LIKE '%@broadcast' "
        "GROUP BY chat_jid HAVING from_me = 1 AND max(ts) < ?1 AND max(ts) > ?2 ORDER BY max(ts) DESC";
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), SQL, &st) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, before);
    sqlite3_bind_int64(st, 2, after);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        str_copy(jids[n], 128, (const char *)sqlite3_column_text(st, 0));
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

static void awaiting_destroy(IAwaitingReplies *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IAwaitingReplies *sqlite_awaiting_replies_create(sqlite3 *db, AccountId account) {
    IAwaitingReplies *a = calloc(1, sizeof(*a));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!a || !scope) { free(a); sqlite_account_scope_destroy(scope); return NULL; }
    a->ctx = scope;
    a->list = awaiting_list;
    a->destroy = awaiting_destroy;
    return a;
}
