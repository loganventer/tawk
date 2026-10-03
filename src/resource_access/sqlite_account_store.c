#include "resource_access/sqlite_account_store.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>

#define COLUMNS "id, label, jid, name, colour, is_primary, agent_access, created_at"

static sqlite3 *db_of(IAccountStore *self) { return (sqlite3 *)self->ctx; }

static int finish(sqlite3 *db, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static void read_row(sqlite3_stmt *st, Account *a) {
    a->id = sqlite3_column_int(st, 0);
    str_copy(a->label, sizeof(a->label), (const char *)sqlite3_column_text(st, 1));
    str_copy(a->jid, sizeof(a->jid), (const char *)sqlite3_column_text(st, 2));
    str_copy(a->name, sizeof(a->name), (const char *)sqlite3_column_text(st, 3));
    a->colour = sqlite3_column_int(st, 4);
    a->is_primary = sqlite3_column_int(st, 5);
    a->agent_access = account_agent_access_parse((const char *)sqlite3_column_text(st, 6));
    a->created_at = sqlite3_column_int64(st, 7);
}

static int store_list(IAccountStore *self, Account *out, int max) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), "SELECT " COLUMNS " FROM accounts ORDER BY id", -1, &st, NULL) != SQLITE_OK) return -1;
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) read_row(st, &out[n++]);
    sqlite3_finalize(st);
    return n;
}

static int store_get(IAccountStore *self, AccountId id, Account *out) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), "SELECT " COLUMNS " FROM accounts WHERE id = ?", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, id);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) read_row(st, out);
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

static int store_add(IAccountStore *self, const char *label, int colour, int64_t now, AccountId *id_out) {
    sqlite3_stmt *st = NULL;
    const char *sql = "INSERT INTO accounts (label, colour, is_primary, agent_access, created_at) VALUES (?, ?, 0, 'off', ?)";
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, label, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, colour);
    sqlite3_bind_int64(st, 3, now);
    if (finish(db_of(self), st) != 0) return -1;
    if (id_out) *id_out = (AccountId)sqlite3_last_insert_rowid(db_of(self));
    return 0;
}

/* Sets one text column of one account; 0 only when that account exists. */
static int set_text(IAccountStore *self, const char *sql, AccountId id, const char *value) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, value ? value : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, id);
    if (finish(db_of(self), st) != 0) return -1;
    return sqlite3_changes(db_of(self)) > 0 ? 0 : -1;
}

static int get_text(IAccountStore *self, const char *sql, AccountId id, char *out, size_t size) {
    if (size) out[0] = '\0';
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, id);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) str_copy(out, size, (const char *)sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

static int store_rename(IAccountStore *self, AccountId id, const char *label) {
    return set_text(self, "UPDATE accounts SET label = ?1 WHERE id = ?2", id, label);
}

static int store_set_primary(IAccountStore *self, AccountId id) {
    Account found;
    if (store_get(self, id, &found) != 0) return -1;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), "UPDATE accounts SET is_primary = (id = ?)", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, id);
    return finish(db_of(self), st);
}

static int store_set_agent_access(IAccountStore *self, AccountId id, AccountAgentAccess access) {
    return set_text(self, "UPDATE accounts SET agent_access = ?1 WHERE id = ?2", id, account_agent_access_name(access));
}

static int store_set_identity(IAccountStore *self, AccountId id, const char *jid, const char *name) {
    sqlite3_stmt *st = NULL;
    /* A name that did not come with the event leaves the one already known. */
    const char *sql = "UPDATE accounts SET jid = ?1, name = CASE WHEN ?2 <> '' THEN ?2 ELSE name END WHERE id = ?3";
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid ? jid : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name ? name : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, id);
    return finish(db_of(self), st);
}

static int store_set_last_chat(IAccountStore *self, AccountId id, const char *jid) {
    return set_text(self, "UPDATE accounts SET last_chat = ?1 WHERE id = ?2", id, jid);
}

static int store_get_last_chat(IAccountStore *self, AccountId id, char *out, size_t size) {
    return get_text(self, "SELECT last_chat FROM accounts WHERE id = ?", id, out, size);
}

static int store_set_self_approval_chats(IAccountStore *self, AccountId id, const char *chats) {
    return set_text(self, "UPDATE accounts SET self_approval_chats = ?1 WHERE id = ?2", id, chats);
}

static int store_get_self_approval_chats(IAccountStore *self, AccountId id, char *out, size_t size) {
    return get_text(self, "SELECT self_approval_chats FROM accounts WHERE id = ?", id, out, size);
}

static int store_remove(IAccountStore *self, AccountId id) {
    /* Every table that keeps an account's rows, then the account itself. The
     * search index follows messages through its triggers. */
    static const char *const TABLES[] = {
        "reactions", "message_receipts", "messages", "chats", "contacts", "jid_aliases", "profiles", "statuses",
        "scheduled_messages", "automation_log",
    };
    sqlite3 *db = db_of(self);
    if (sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) return -1;
    int ok = 1;
    for (size_t i = 0; ok && i < sizeof(TABLES) / sizeof(TABLES[0]); i++) {
        char sql[96];
        snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE account_id = ?", TABLES[i]);
        sqlite3_stmt *st = NULL;
        ok = sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_int(st, 1, id);
            ok = finish(db, st) == 0;
        }
    }
    sqlite3_stmt *st = NULL;
    ok = ok && sqlite3_prepare_v2(db, "DELETE FROM accounts WHERE id = ?", -1, &st, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(st, 1, id);
        ok = finish(db, st) == 0 && sqlite3_changes(db) > 0;
    }
    if (!ok) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }
    return sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static void store_destroy(IAccountStore *self) { free(self); }

IAccountStore *sqlite_account_store_create(sqlite3 *db) {
    IAccountStore *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx = db;
    s->list = store_list;
    s->get = store_get;
    s->add = store_add;
    s->rename = store_rename;
    s->set_primary = store_set_primary;
    s->set_agent_access = store_set_agent_access;
    s->set_identity = store_set_identity;
    s->set_last_chat = store_set_last_chat;
    s->get_last_chat = store_get_last_chat;
    s->set_self_approval_chats = store_set_self_approval_chats;
    s->get_self_approval_chats = store_get_self_approval_chats;
    s->remove = store_remove;
    s->destroy = store_destroy;
    return s;
}
