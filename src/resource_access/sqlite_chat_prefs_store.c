#include "resource_access/sqlite_chat_prefs_store.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

/* The store and the smaller contract it hands out, over the same table. */
typedef struct ChatPrefsState {
    sqlite3             *db;
    IChatTranscriptPrefs transcripts;
    IChatSummaryPrefs    summaries;
} ChatPrefsState;

static sqlite3 *db_of(IChatPrefsStore *self) { return ((ChatPrefsState *)self->ctx)->db; }

static int finish(sqlite3 *db, sqlite3_stmt *st) {
    int rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) LOG_WARN("sqlite: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 0 : -1;
}

static ChatMergeChoice merge_of(int stored) {
    return stored == CHAT_MERGE_ALWAYS || stored == CHAT_MERGE_NEVER ? (ChatMergeChoice)stored : CHAT_MERGE_FOLLOW;
}

static ChatTranscriptChoice show_of(int stored) {
    return stored == CHAT_TRANSCRIPT_ALWAYS || stored == CHAT_TRANSCRIPT_NEVER ? (ChatTranscriptChoice)stored : CHAT_TRANSCRIPT_FOLLOW;
}

static int prefs_get(IChatPrefsStore *self, const char *jid, ChatPrefs *out) {
    memset(out, 0, sizeof(*out));
    str_copy(out->jid, sizeof(out->jid), jid);
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT send_account, merge, show_transcripts, transcribe_off, tldr FROM chat_prefs WHERE jid = ?";
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        out->send_account = sqlite3_column_int(st, 0);
        out->merge = merge_of(sqlite3_column_int(st, 1));
        out->show_transcripts = show_of(sqlite3_column_int(st, 2));
        out->transcribe_off = sqlite3_column_int(st, 3) != 0;
        out->tldr = sqlite3_column_int(st, 4) != 0;
    }
    sqlite3_finalize(st);
    return 0;
}

/* Sets one number for one chat, making its row when it has none. */
static int set_int_in(sqlite3 *db, const char *sql, const char *jid, int value) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, jid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, value);
    return finish(db, st);
}

static int set_int(IChatPrefsStore *self, const char *sql, const char *jid, int value) {
    return set_int_in(db_of(self), sql, jid, value);
}

static int transcripts_set_show(IChatTranscriptPrefs *self, const char *jid, ChatTranscriptChoice choice) {
    return set_int_in((sqlite3 *)self->ctx, "INSERT INTO chat_prefs (jid, show_transcripts) VALUES (?1, ?2) "
                      "ON CONFLICT(jid) DO UPDATE SET show_transcripts = excluded.show_transcripts", jid, choice);
}

static int transcripts_set_transcribe_off(IChatTranscriptPrefs *self, const char *jid, int off) {
    return set_int_in((sqlite3 *)self->ctx, "INSERT INTO chat_prefs (jid, transcribe_off) VALUES (?1, ?2) "
                      "ON CONFLICT(jid) DO UPDATE SET transcribe_off = excluded.transcribe_off", jid, off ? 1 : 0);
}

static int summaries_set_tldr(IChatSummaryPrefs *self, const char *jid, int on) {
    return set_int_in((sqlite3 *)self->ctx, "INSERT INTO chat_prefs (jid, tldr) VALUES (?1, ?2) "
                      "ON CONFLICT(jid) DO UPDATE SET tldr = excluded.tldr", jid, on ? 1 : 0);
}

static int prefs_set_send_account(IChatPrefsStore *self, const char *jid, AccountId account) {
    return set_int(self, "INSERT INTO chat_prefs (jid, send_account) VALUES (?1, ?2) "
                         "ON CONFLICT(jid) DO UPDATE SET send_account = excluded.send_account", jid, account);
}

static int prefs_set_merge(IChatPrefsStore *self, const char *jid, ChatMergeChoice merge) {
    return set_int(self, "INSERT INTO chat_prefs (jid, merge) VALUES (?1, ?2) "
                         "ON CONFLICT(jid) DO UPDATE SET merge = excluded.merge", jid, merge);
}

static int prefs_list_send_accounts(IChatPrefsStore *self, ChatPrefs *out, int max) {
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT jid, send_account, merge FROM chat_prefs WHERE send_account <> 0 ORDER BY jid";
    if (sqlite3_prepare_v2(db_of(self), sql, -1, &st, NULL) != SQLITE_OK) return -1;
    int n = 0;
    while (n < max && sqlite3_step(st) == SQLITE_ROW) {
        memset(&out[n], 0, sizeof(out[n]));
        str_copy(out[n].jid, sizeof(out[n].jid), (const char *)sqlite3_column_text(st, 0));
        out[n].send_account = sqlite3_column_int(st, 1);
        out[n].merge = merge_of(sqlite3_column_int(st, 2));
        n++;
    }
    sqlite3_finalize(st);
    return n;
}

static int prefs_forget_account(IChatPrefsStore *self, AccountId account) {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db_of(self), "UPDATE chat_prefs SET send_account = 0 WHERE send_account = ?", -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, account);
    return finish(db_of(self), st);
}

static int prefs_reassign_jid(IChatPrefsStore *self, const char *from, const char *to) {
    /* What was chosen under the address that stays wins over the one that goes. */
    static const char *const SQL[] = {
        "INSERT OR IGNORE INTO chat_prefs (jid, send_account, merge, show_transcripts, transcribe_off, tldr) "
        "SELECT ?2, send_account, merge, show_transcripts, transcribe_off, tldr FROM chat_prefs WHERE jid = ?1",
        "DELETE FROM chat_prefs WHERE jid = ?1",
    };
    for (int i = 0; i < 2; i++) {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db_of(self), SQL[i], -1, &st, NULL) != SQLITE_OK) return -1;
        sqlite3_bind_text(st, 1, from, -1, SQLITE_TRANSIENT);
        if (i == 0) sqlite3_bind_text(st, 2, to, -1, SQLITE_TRANSIENT);
        if (finish(db_of(self), st) != 0) return -1;
    }
    return 0;
}

static void prefs_destroy(IChatPrefsStore *self) {
    if (!self) return;
    free(self->ctx);
    free(self);
}

IChatTranscriptPrefs *sqlite_chat_prefs_store_transcripts(IChatPrefsStore *self) {
    return self ? &((ChatPrefsState *)self->ctx)->transcripts : NULL;
}

IChatSummaryPrefs *sqlite_chat_prefs_store_summaries(IChatPrefsStore *self) {
    return self ? &((ChatPrefsState *)self->ctx)->summaries : NULL;
}

IChatPrefsStore *sqlite_chat_prefs_store_create(sqlite3 *db) {
    IChatPrefsStore *s = calloc(1, sizeof(*s));
    ChatPrefsState *state = calloc(1, sizeof(*state));
    if (!s || !state) { free(s); free(state); return NULL; }
    state->db = db;
    state->transcripts.ctx = db;
    state->transcripts.set_show = transcripts_set_show;
    state->transcripts.set_transcribe_off = transcripts_set_transcribe_off;
    state->summaries.ctx = db;
    state->summaries.set_tldr = summaries_set_tldr;
    s->ctx = state;
    s->get = prefs_get;
    s->set_send_account = prefs_set_send_account;
    s->set_merge = prefs_set_merge;
    s->list_send_accounts = prefs_list_send_accounts;
    s->forget_account = prefs_forget_account;
    s->reassign_jid = prefs_reassign_jid;
    s->destroy = prefs_destroy;
    return s;
}
