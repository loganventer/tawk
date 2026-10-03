#include "resource_access/sqlite_jid_alias_store.h"
#include "resource_access/jid_alias.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

typedef struct AliasStore {
    SqliteAccountScope scope;
    JidAlias *items;
    int       count;
    int       cap;
} AliasStore;

static AliasStore *ctx_of(IJidAliasStore *self) { return (AliasStore *)self->ctx; }

static JidAlias *find(AliasStore *a, const char *alias) {
    for (int i = 0; i < a->count; i++) if (strcmp(a->items[i].alias, alias) == 0) return &a->items[i];
    return NULL;
}

static void remember(AliasStore *a, const char *alias, const char *canonical) {
    JidAlias *hit = find(a, alias);
    if (!hit) {
        if (a->count == a->cap) {
            int cap = a->cap ? a->cap * 2 : 256;
            JidAlias *grown = realloc(a->items, (size_t)cap * sizeof(JidAlias));
            if (!grown) return;
            a->items = grown;
            a->cap = cap;
        }
        hit = &a->items[a->count++];
        str_copy(hit->alias, sizeof(hit->alias), alias);
    }
    str_copy(hit->canonical, sizeof(hit->canonical), canonical);
}

static int alias_put(IJidAliasStore *self, const char *alias, const char *canonical) {
    AliasStore *a = ctx_of(self);
    JidAlias *hit = find(a, alias);
    if (hit && strcmp(hit->canonical, canonical) == 0) return 0;
    sqlite3_stmt *st = NULL;
    const char *sql = "INSERT INTO jid_aliases (alias, canonical, account_id) VALUES (?, ?, {acct}) "
                      "ON CONFLICT(account_id, alias) DO UPDATE SET canonical = excluded.canonical";
    if (sqlite_account_scope_prepare(&a->scope, sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, alias, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, canonical, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) { LOG_WARN("sqlite: %s", sqlite3_errmsg(a->scope.db)); return -1; }
    remember(a, alias, canonical);
    return 1;   /* new or changed */
}

static const char *alias_resolve(IJidAliasStore *self, const char *jid) {
    JidAlias *hit = jid ? find(ctx_of(self), jid) : NULL;
    return hit ? hit->canonical : jid;
}

static void alias_destroy(IJidAliasStore *self) {
    if (!self) return;
    free(ctx_of(self)->items);
    free(self->ctx);
    free(self);
}

IJidAliasStore *sqlite_jid_alias_store_create(sqlite3 *db, AccountId account) {
    IJidAliasStore *s = calloc(1, sizeof(*s));
    AliasStore *a = calloc(1, sizeof(*a));
    if (!s || !a) { free(s); free(a); return NULL; }
    a->scope.db = db;
    a->scope.account = account;
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(&a->scope, "SELECT alias, canonical FROM jid_aliases WHERE account_id = {acct}", &st) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            remember(a, (const char *)sqlite3_column_text(st, 0), (const char *)sqlite3_column_text(st, 1));
        }
        sqlite3_finalize(st);
    }
    s->ctx = a;
    s->put = alias_put;
    s->resolve = alias_resolve;
    s->destroy = alias_destroy;
    return s;
}
