#ifndef APP_RESOURCE_ACCESS_SQLITE_REACTION_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_REACTION_STORE_H

#include <sqlite3.h>

#include "contracts/i_reaction_store.h"
#include "core/account_id.h"

/* A store over the reactions one account has seen. */
IReactionStore *sqlite_reaction_store_create(sqlite3 *db, AccountId account);

#endif
