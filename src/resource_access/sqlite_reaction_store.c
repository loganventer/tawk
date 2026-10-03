#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SqliteAccountScope *scope_of(IReactionStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(IReactionStore *self) { return scope_of(self)->db; }

static int run(IReactionStore *self, const char *sql, const char *a, const char *b, const char *c) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, a, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, b, -1, SQLITE_TRANSIENT);
    if (c) sqlite3_bind_text(st, 3, c, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db_of(self)));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int reaction_put(IReactionStore *self, const char *message_id, const char *sender, const char *emoji) {
    if (!emoji || !emoji[0]) {
        return run(self, "DELETE FROM reactions WHERE message_id = ? AND sender_jid = ? AND account_id = {acct}", message_id, sender, NULL);
    }
    return run(self, "INSERT INTO reactions (message_id, sender_jid, emoji, account_id) VALUES (?, ?, ?, {acct}) "
                     "ON CONFLICT(account_id, message_id, sender_jid) DO UPDATE SET emoji = excluded.emoji",
               message_id, sender, emoji);
}

static void reaction_summary(IReactionStore *self, const char *message_id, char *out, size_t size) {
    out[0] = '\0';
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT emoji, COUNT(*) FROM reactions WHERE account_id = {acct} AND message_id = ? GROUP BY emoji ORDER BY COUNT(*) DESC LIMIT 4";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, message_id, -1, SQLITE_TRANSIENT);
    size_t used = 0;
    while (sqlite3_step(st) == SQLITE_ROW && used < size) {
        char emoji[32];
        str_copy(emoji, sizeof(emoji), (const char *)sqlite3_column_text(st, 0));
        str_strip_controls(emoji);
        int count = sqlite3_column_int(st, 1);
        int n = count > 1 ? snprintf(out + used, size - used, "%s%s %d", used ? "  " : "", emoji, count)
                          : snprintf(out + used, size - used, "%s%s", used ? "  " : "", emoji);
        if (n < 0) break;
        used += (size_t)n;
    }
    sqlite3_finalize(st);
}

static int reaction_list(IReactionStore *self, const char *message_id, Reaction *out, int max) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT sender_jid, emoji FROM reactions WHERE account_id = {acct} AND message_id = ?", &st) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, message_id, -1, SQLITE_TRANSIENT);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        str_copy(out[n].sender, sizeof(out[n].sender), (const char *)sqlite3_column_text(st, 0));
        str_copy(out[n].emoji, sizeof(out[n].emoji), (const char *)sqlite3_column_text(st, 1));
        str_strip_controls(out[n].emoji);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

static int reaction_reassign_sender(IReactionStore *self, const char *from, const char *to) {
    return run(self, "UPDATE OR REPLACE reactions SET sender_jid = ?2 WHERE account_id = {acct} AND sender_jid = ?1", from, to, NULL);
}

static void reaction_destroy(IReactionStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IReactionStore *sqlite_reaction_store_create(sqlite3 *db, AccountId account) {
    IReactionStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->put = reaction_put;
    s->summary = reaction_summary;
    s->list = reaction_list;
    s->reassign_sender = reaction_reassign_sender;
    s->destroy = reaction_destroy;
    return s;
}
