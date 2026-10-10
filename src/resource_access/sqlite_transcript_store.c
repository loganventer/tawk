#include "resource_access/sqlite_transcript_store.h"
#include "resource_access/sqlite_account_scope.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>

static SqliteAccountScope *scope_of(ITranscriptStore *self) { return (SqliteAccountScope *)self->ctx; }
static sqlite3 *db_of(ITranscriptStore *self) { return scope_of(self)->db; }

static int finish(ITranscriptStore *self, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db_of(self)));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int transcript_save(ITranscriptStore *self, const Transcript *t) {
    if (!t || !t->message_id[0] || !t->text) return -1;
    static const char *const SQL =
        "INSERT INTO transcripts (message_id, language, text, model, source, created_at, account_id) VALUES (?, ?, ?, ?, ?, ?, {acct}) "
        "ON CONFLICT(account_id, message_id, language) DO UPDATE SET "
        "text = excluded.text, model = excluded.model, source = excluded.source, created_at = excluded.created_at";
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), SQL, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, t->message_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, t->language, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, t->text, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, t->model, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, t->source, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 6, t->created_at);
    return finish(self, st);
}

static int transcript_find(ITranscriptStore *self, const char *message_id, Transcript **out, int *count) {
    *out = NULL;
    *count = 0;
    if (!message_id || !message_id[0]) return 0;
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT language, text, model, source, created_at FROM transcripts "
                      "WHERE account_id = {acct} AND message_id = ? ORDER BY created_at DESC, language";
    if (sqlite_account_scope_prepare(scope_of(self), sql, &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, message_id, -1, SQLITE_TRANSIENT);
    int n = 0, cap = 0;
    Transcript *items = NULL;
    while (sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            int grown_cap = cap ? cap * 2 : 2;
            Transcript *grown = realloc(items, (size_t)grown_cap * sizeof(Transcript));
            if (!grown) break;
            items = grown;
            cap = grown_cap;
        }
        Transcript *t = &items[n++];
        transcript_init(t);
        str_copy(t->message_id, sizeof(t->message_id), message_id);
        str_copy(t->language, sizeof(t->language), (const char *)sqlite3_column_text(st, 0));
        transcript_set_text(t, (const char *)sqlite3_column_text(st, 1));
        str_copy(t->model, sizeof(t->model), (const char *)sqlite3_column_text(st, 2));
        str_copy(t->source, sizeof(t->source), (const char *)sqlite3_column_text(st, 3));
        t->created_at = sqlite3_column_int64(st, 4);
    }
    sqlite3_finalize(st);
    *out = items;
    *count = n;
    return 0;
}

static int transcript_remove(ITranscriptStore *self, const char *message_id) {
    sqlite3_stmt *st = NULL;
    if (sqlite_account_scope_prepare(scope_of(self), "DELETE FROM transcripts WHERE account_id = {acct} AND message_id = ?", &st) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, message_id, -1, SQLITE_TRANSIENT);
    return finish(self, st);
}

static void transcript_destroy(ITranscriptStore *self) {
    if (!self) return;
    sqlite_account_scope_destroy(scope_of(self));
    free(self);
}

ITranscriptStore *sqlite_transcript_store_create(sqlite3 *db, AccountId account) {
    ITranscriptStore *s = calloc(1, sizeof(*s));
    SqliteAccountScope *scope = sqlite_account_scope_create(db, account);
    if (!s || !scope) { free(s); sqlite_account_scope_destroy(scope); return NULL; }
    s->ctx = scope;
    s->save = transcript_save;
    s->find = transcript_find;
    s->remove = transcript_remove;
    s->destroy = transcript_destroy;
    return s;
}
