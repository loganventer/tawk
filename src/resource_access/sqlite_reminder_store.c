#include "resource_access/sqlite_reminder_store.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>

static sqlite3 *db_of(IReminderStore *self) { return (sqlite3 *)self->ctx; }

static int finish(IReminderStore *self, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db_of(self)));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int reminder_set(IReminderStore *self, const ChatReminder *r) {
    if (!r || !r->jid[0]) return -1;
    sqlite3_stmt *st = NULL;
    static const char *const SQL = "INSERT INTO chat_reminders (jid, due_at, created_at) VALUES (?, ?, ?) "
                                   "ON CONFLICT(jid) DO UPDATE SET due_at = excluded.due_at, created_at = excluded.created_at";
    if (sqlite3_prepare_v2(db_of(self), SQL, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, r->jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, r->due_at);
    sqlite3_bind_int64(st, 3, r->created_at);
    return finish(self, st);
}

static int reminder_clear(IReminderStore *self, const char *jid) {
    sqlite3_stmt *st = NULL;
    if (!jid || sqlite3_prepare_v2(db_of(self), "DELETE FROM chat_reminders WHERE jid = ?", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    return finish(self, st);
}

static int reminder_list(IReminderStore *self, ChatReminder *out, int max) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), "SELECT jid, due_at, created_at FROM chat_reminders ORDER BY due_at, jid", -1, &st, NULL) != SQLITE_OK) return -1;
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        str_copy(out[n].jid, sizeof(out[n].jid), (const char *)sqlite3_column_text(st, 0));
        out[n].due_at = sqlite3_column_int64(st, 1);
        out[n].created_at = sqlite3_column_int64(st, 2);
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

static void reminder_destroy(IReminderStore *self) { free(self); }

IReminderStore *sqlite_reminder_store_create(sqlite3 *db) {
    IReminderStore *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx = db;
    s->set = reminder_set;
    s->clear = reminder_clear;
    s->list = reminder_list;
    s->destroy = reminder_destroy;
    return s;
}
