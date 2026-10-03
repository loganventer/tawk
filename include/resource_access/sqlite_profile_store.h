#ifndef APP_RESOURCE_ACCESS_SQLITE_PROFILE_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_PROFILE_STORE_H

#include <sqlite3.h>

#include "contracts/i_profile_store.h"
#include "core/account_id.h"

/* Profiles in the `profiles` table (migration 6). Does not own `db`. */
/* A store over the profiles one account has seen. */
IProfileStore *sqlite_profile_store_create(sqlite3 *db, AccountId account);

#endif
