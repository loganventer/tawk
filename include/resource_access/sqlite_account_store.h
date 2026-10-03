#ifndef APP_RESOURCE_ACCESS_SQLITE_ACCOUNT_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_ACCOUNT_STORE_H

#include <sqlite3.h>

#include "contracts/i_account_store.h"

IAccountStore *sqlite_account_store_create(sqlite3 *db);

#endif
