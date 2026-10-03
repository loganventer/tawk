#ifndef APP_RESOURCE_ACCESS_SQLITE_JID_ALIAS_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_JID_ALIAS_STORE_H

#include <sqlite3.h>

#include "contracts/i_jid_alias_store.h"
#include "core/account_id.h"

/* Persists aliases in SQLite and keeps them all in memory for lookups. */
/* A store over the address aliases one account has learned. */
IJidAliasStore *sqlite_jid_alias_store_create(sqlite3 *db, AccountId account);

#endif
