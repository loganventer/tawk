#include "resource_access/sqlite_contact_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>

static SqliteAccountScope *scope_of(IContactStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(IContactStore *self) { return scope_of(self)->db; }

static int store_upsert(IContactStore *self, const Contact *c) {
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO contacts (jid, name, push_name, account_id) VALUES (?,?,?,{acct}) "
        "ON CONFLICT(account_id, jid) DO UPDATE SET "
        " name = CASE WHEN excluded.name <> '' THEN excluded.name ELSE name END,"
        " push_name = CASE WHEN excluded.push_name <> '' THEN excluded.push_name ELSE push_name END";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, c->jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, c->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, c->push_name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db_of(self)));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int store_get(IContactStore *self, const char *jid, Contact *out) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "SELECT jid, name, push_name FROM contacts WHERE account_id = {acct} AND jid = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) {
        contact_init(out, (const char *)sqlite3_column_text(st, 0));
        str_copy(out->name, sizeof(out->name), (const char *)sqlite3_column_text(st, 1));
        str_copy(out->push_name, sizeof(out->push_name), (const char *)sqlite3_column_text(st, 2));
    }
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

static int store_merge(IContactStore *self, const char *from, const char *to) {
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO contacts (account_id, jid, name, push_name) SELECT {acct}, ?2, name, push_name FROM contacts WHERE account_id = {acct} AND jid = ?1 "
        "ON CONFLICT(account_id, jid) DO UPDATE SET "
        " name = CASE WHEN contacts.name = '' THEN excluded.name ELSE contacts.name END,"
        " push_name = CASE WHEN contacts.push_name = '' THEN excluded.push_name ELSE contacts.push_name END";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, from, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, to, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

/* `text` as a LIKE pattern that matches it anywhere, with LIKE's own wildcards taken literally. */
static void like_pattern(const char *text, char *out, size_t size) {
    size_t k = 0;
    if (k + 1 < size) out[k++] = '%';
    for (const char *p = text; *p && k + 3 < size; p++) {
        if (*p == '%' || *p == '_' || *p == '\\') out[k++] = '\\';
        out[k++] = *p;
    }
    if (k + 1 < size) out[k++] = '%';
    out[k] = '\0';
}

static int store_find_by_name(IContactStore *self, const char *text, Contact *out, int max) {
    if (!text || !*text || !out || max <= 0) return 0;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT jid, name, push_name FROM contacts WHERE account_id = {acct} AND jid LIKE '%@s.whatsapp.net' "
        "AND (name LIKE ?1 ESCAPE '\\' OR push_name LIKE ?1 ESCAPE '\\') ORDER BY name, push_name, jid LIMIT ?2";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    char pattern[300];
    like_pattern(text, pattern, sizeof(pattern));
    sqlite3_bind_text(st, 1, pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, max);
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        contact_init(&out[n], (const char *)sqlite3_column_text(st, 0));
        str_copy(out[n].name, sizeof(out[n].name), (const char *)sqlite3_column_text(st, 1));
        str_copy(out[n].push_name, sizeof(out[n].push_name), (const char *)sqlite3_column_text(st, 2));
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

static void store_destroy(IContactStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IContactStore *sqlite_contact_store_create(sqlite3 *db, AccountId account) {
    IContactStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->upsert = store_upsert;
    s->get = store_get;
    s->merge = store_merge;
    s->destroy = store_destroy;
    s->find_by_name = store_find_by_name;
    return s;
}
