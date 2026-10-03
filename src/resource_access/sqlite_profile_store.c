#include "resource_access/sqlite_profile_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

static SqliteAccountScope *scope_of(IProfileStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(IProfileStore *self) { return scope_of(self)->db; }

static int run(sqlite3 *db, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static void text_col(sqlite3_stmt *st, int col, char *out, size_t size) {
    const unsigned char *t = sqlite3_column_text(st, col);
    str_copy(out, size, t ? (const char *)t : "");
}

static int store_get(IProfileStore *self, const char *jid, ContactProfile *p) {
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT about, verified_name, is_business, business_category, business_address, business_email,"
                      " is_group, group_subject, group_description, group_owner, group_created, participant_count,"
                      " participants, picture, picture_full, picture_none, blocked, fetched_at FROM profiles WHERE account_id = {acct} AND jid = ?";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    int found = sqlite3_step(st) == SQLITE_ROW;
    if (found) {
        contact_profile_init(p, jid);
        text_col(st, 0, p->about, sizeof(p->about));
        text_col(st, 1, p->verified_name, sizeof(p->verified_name));
        p->is_business = sqlite3_column_int(st, 2);
        text_col(st, 3, p->business_category, sizeof(p->business_category));
        text_col(st, 4, p->business_address, sizeof(p->business_address));
        text_col(st, 5, p->business_email, sizeof(p->business_email));
        p->is_group = sqlite3_column_int(st, 6);
        text_col(st, 7, p->group_subject, sizeof(p->group_subject));
        text_col(st, 8, p->group_description, sizeof(p->group_description));
        text_col(st, 9, p->group_owner, sizeof(p->group_owner));
        p->group_created = sqlite3_column_int64(st, 10);
        p->participant_count = sqlite3_column_int(st, 11);
        const unsigned char *members = sqlite3_column_text(st, 12);
        p->participants = members ? strdup((const char *)members) : NULL;
        text_col(st, 13, p->picture, sizeof(p->picture));
        text_col(st, 14, p->picture_full, sizeof(p->picture_full));
        p->picture_none = sqlite3_column_int(st, 15);
        p->blocked = sqlite3_column_int(st, 16);
        p->fetched_at = sqlite3_column_int64(st, 17);
    }
    sqlite3_finalize(st);
    return found ? 0 : -1;
}

/* Makes sure a row exists, so the updates below have something to change. */
static void ensure_row(IProfileStore *self, const char *jid) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "INSERT OR IGNORE INTO profiles (jid, account_id) VALUES (?, {acct})", &st) != SQLITE_OK) return;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    run(db_of(self), st);
}

static int store_save_details(IProfileStore *self, const ContactProfile *p) {
    ensure_row(self, p->jid);
    sqlite3_stmt *st = NULL;
    const char *sql = "UPDATE profiles SET about = ?, verified_name = ?, is_business = ?, business_category = ?,"
                      " business_address = ?, business_email = ?, is_group = ?, group_subject = ?, group_description = ?,"
                      " group_owner = ?, group_created = ?, participant_count = ?, participants = ?, fetched_at = ?"
                      " WHERE account_id = {acct} AND jid = ?";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, p->about, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, p->verified_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, p->is_business);
    sqlite3_bind_text(st, 4, p->business_category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, p->business_address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, p->business_email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, p->is_group);
    sqlite3_bind_text(st, 8, p->group_subject, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, p->group_description, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 10, p->group_owner, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 11, p->group_created);
    sqlite3_bind_int(st, 12, p->participant_count);
    if (p->participants) sqlite3_bind_text(st, 13, p->participants, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(st, 13);
    sqlite3_bind_int64(st, 14, p->fetched_at);
    sqlite3_bind_text(st, 15, p->jid, -1, SQLITE_TRANSIENT);
    return run(db_of(self), st);
}

static int store_set_picture(IProfileStore *self, const char *jid, const char *path, int full, int none) {
    ensure_row(self, jid);
    sqlite3_stmt *st = NULL;
    const char *sql = none ? "UPDATE profiles SET picture = '', picture_full = '', picture_none = 1 WHERE account_id = {acct} AND jid = ?2"
                    : full ? "UPDATE profiles SET picture_full = ?1, picture_none = 0 WHERE account_id = {acct} AND jid = ?2"
                           : "UPDATE profiles SET picture = ?1, picture_none = 0 WHERE account_id = {acct} AND jid = ?2";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    if (!none) sqlite3_bind_text(st, 1, path ? path : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, jid, -1, SQLITE_TRANSIENT);
    return run(db_of(self), st);
}

static int store_forget_picture(IProfileStore *self, const char *jid) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "UPDATE profiles SET picture = '', picture_full = '', picture_none = 0 WHERE account_id = {acct} AND jid = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    return run(db_of(self), st);
}

static int store_set_blocklist(IProfileStore *self, const char *jids) {
    sqlite3 *db = db_of(self);
    sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);
    sqlite3_stmt *clear = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "UPDATE profiles SET blocked = 0 WHERE account_id = {acct} AND blocked = 1", &clear) == SQLITE_OK) {
        run(db, clear);
    }
    char *copy = strdup(jids ? jids : "");
    char *save = NULL;
    for (char *jid = copy ? strtok_r(copy, "\n", &save) : NULL; jid; jid = strtok_r(NULL, "\n", &save)) {
        ensure_row(self, jid);
        sqlite3_stmt *st = NULL;
        if (sqlite_account_scope_prepare(scope_of(self), "UPDATE profiles SET blocked = 1 WHERE account_id = {acct} AND jid = ?", &st) != SQLITE_OK) continue;
        sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
        run(db, st);
    }
    free(copy);
    return sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static void store_destroy(IProfileStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

IProfileStore *sqlite_profile_store_create(sqlite3 *db, AccountId account) {
    IProfileStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->get = store_get;
    s->save_details = store_save_details;
    s->set_picture = store_set_picture;
    s->forget_picture = store_forget_picture;
    s->set_blocklist = store_set_blocklist;
    s->destroy = store_destroy;
    return s;
}
