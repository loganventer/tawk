#include "resource_access/sqlite_account_scope.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKEN     "{acct}"
#define TOKEN_LEN (sizeof(TOKEN) - 1)
#define ID_MAX    12                       /* digits of an int, with its sign */

SqliteAccountScope *sqlite_account_scope_create(sqlite3 *db, AccountId account) {
    SqliteAccountScope *scope = calloc(1, sizeof(*scope));
    if (!scope) return NULL;
    scope->db = db;
    scope->account = account;
    return scope;
}

void sqlite_account_scope_destroy(SqliteAccountScope *scope) {
    free(scope);
}

int sqlite_account_scope_prepare(const SqliteAccountScope *scope, const char *sql, sqlite3_stmt **out) {
    *out = NULL;
    size_t tokens = 0;
    for (const char *p = strstr(sql, TOKEN); p; p = strstr(p + TOKEN_LEN, TOKEN)) tokens++;
    if (tokens == 0) return sqlite3_prepare_v2(scope->db, sql, -1, out, NULL);

    char id[ID_MAX];
    int id_len = snprintf(id, sizeof(id), "%d", scope->account);
    char *bound = malloc(strlen(sql) + tokens * (size_t)id_len + 1);
    if (!bound) return SQLITE_NOMEM;
    char *w = bound;
    for (const char *p = sql; *p;) {
        if (strncmp(p, TOKEN, TOKEN_LEN) == 0) {
            memcpy(w, id, (size_t)id_len);
            w += id_len;
            p += TOKEN_LEN;
        } else {
            *w++ = *p++;
        }
    }
    *w = '\0';
    int rc = sqlite3_prepare_v2(scope->db, bound, -1, out, NULL);
    free(bound);
    return rc;
}
