#include "resource_access/sqlite_automation_log.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

#define KEEP_SQL "5000"   /* entries kept; older ones are removed now and then */

static sqlite3 *db_of(IAutomationLog *self) { return (sqlite3 *)self->ctx; }

static const char *text_at(sqlite3_stmt *st, int col) {
    const char *v = (const char *)sqlite3_column_text(st, col);
    return v ? v : "";
}

static int log_append(IAutomationLog *self, const AutomationEntry *e) {
    sqlite3_stmt *st = NULL;
    const char *sql = "INSERT INTO automation_log (at, origin, client, op, chat_jid, summary, outcome, account_id) VALUES (?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(st, 1, e->at);
    sqlite3_bind_text(st, 2, control_origin_name(e->origin), -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, e->client, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, e->op, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, e->chat_jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, e->summary, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, automation_outcome_name(e->outcome), -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 8, e->account > ACCOUNT_ID_NONE ? e->account : ACCOUNT_ID_FIRST);
    int rc = sqlite3_step(st) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(st);
    if (rc == 0 && sqlite3_last_insert_rowid(db_of(self)) % 100 == 0) {
        sqlite3_exec(db_of(self), "DELETE FROM automation_log WHERE id <= (SELECT max(id) FROM automation_log) - " KEEP_SQL, NULL, NULL, NULL);
    }
    return rc;
}

static int log_recent(IAutomationLog *self, int limit, AutomationEntry **out, int *count) {
    *out = NULL;
    *count = 0;
    if (limit <= 0) return 0;
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT at, origin, client, op, chat_jid, summary, outcome, account_id FROM automation_log ORDER BY id DESC LIMIT ?";
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, limit);
    AutomationEntry *items = calloc((size_t)limit, sizeof(*items));
    if (!items) { sqlite3_finalize(st); return -1; }
    int n = 0;
    while (n < limit && sqlite3_step(st) == SQLITE_ROW) {
        AutomationEntry *e = &items[n++];
        e->at = sqlite3_column_int64(st, 0);
        ControlOrigin origin = CONTROL_ORIGIN_MCP;
        control_origin_parse(text_at(st, 1), &origin);
        e->origin = origin;
        str_copy(e->client, sizeof(e->client), text_at(st, 2));
        str_copy(e->op, sizeof(e->op), text_at(st, 3));
        str_copy(e->chat_jid, sizeof(e->chat_jid), text_at(st, 4));
        str_copy(e->summary, sizeof(e->summary), text_at(st, 5));
        e->outcome = automation_outcome_parse(text_at(st, 6));
        e->account = sqlite3_column_int(st, 7);
    }
    sqlite3_finalize(st);
    *out = items;
    *count = n;
    return 0;
}

static void log_destroy(IAutomationLog *self) { free(self); }

IAutomationLog *sqlite_automation_log_create(sqlite3 *db) {
    IAutomationLog *log = calloc(1, sizeof(*log));
    if (!log) return NULL;
    log->ctx = db;
    log->append = log_append;
    log->recent = log_recent;
    log->destroy = log_destroy;
    return log;
}
