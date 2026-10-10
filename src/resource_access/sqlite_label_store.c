#include "resource_access/sqlite_label_store.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>

static sqlite3 *db_of(ILabelStore *self) { return (sqlite3 *)self->ctx; }

static int run(ILabelStore *self, const char *sql, const char *jid, const char *label) {
    sqlite3_stmt *st = NULL;
    if (!jid || !jid[0] || !label || !label[0]) return -1;
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, label, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db_of(self)));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int label_add(ILabelStore *self, const char *jid, const char *label) {
    return run(self, "INSERT OR IGNORE INTO chat_labels (jid, label) VALUES (?, ?)", jid, label);
}

static int label_remove(ILabelStore *self, const char *jid, const char *label) {
    return run(self, "DELETE FROM chat_labels WHERE jid = ? AND label = ?", jid, label);
}

static int label_list(ILabelStore *self, ChatLabel *out, int max) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), "SELECT jid, label FROM chat_labels ORDER BY label, jid", -1, &st, NULL) != SQLITE_OK) return -1;
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        str_copy(out[n].jid, sizeof(out[n].jid), (const char *)sqlite3_column_text(st, 0));
        str_copy(out[n].label, sizeof(out[n].label), (const char *)sqlite3_column_text(st, 1));
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

static void label_destroy(ILabelStore *self) { free(self); }

ILabelStore *sqlite_label_store_create(sqlite3 *db) {
    ILabelStore *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx = db;
    s->add = label_add;
    s->remove = label_remove;
    s->list = label_list;
    s->destroy = label_destroy;
    return s;
}
