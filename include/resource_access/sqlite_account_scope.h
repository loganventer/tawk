#ifndef APP_RESOURCE_ACCESS_SQLITE_ACCOUNT_SCOPE_H
#define APP_RESOURCE_ACCESS_SQLITE_ACCOUNT_SCOPE_H

#include <sqlite3.h>

#include "core/account_id.h"

/* The database together with the one account a store works for. Every
 * statement such a store runs is limited to that account's rows. */
typedef struct SqliteAccountScope {
    sqlite3  *db;
    AccountId account;
} SqliteAccountScope;

SqliteAccountScope *sqlite_account_scope_create(sqlite3 *db, AccountId account);
void sqlite_account_scope_destroy(SqliteAccountScope *scope);

/* Prepares `sql` with every {acct} in it replaced by the scope's account id.
 * The id is a number of ours, never text from outside, so it is safe in the
 * statement itself, and the statement's own parameters keep their positions.
 * Returns what sqlite3_prepare_v2 returns. */
int sqlite_account_scope_prepare(const SqliteAccountScope *scope, const char *sql, sqlite3_stmt **out);

#endif
