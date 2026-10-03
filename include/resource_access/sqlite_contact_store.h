#ifndef APP_RESOURCE_ACCESS_SQLITE_CONTACT_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_CONTACT_STORE_H

#include <sqlite3.h>

#include "contracts/i_contact_store.h"
#include "core/account_id.h"

/* A store over the contacts of one account. */
IContactStore *sqlite_contact_store_create(sqlite3 *db, AccountId account);

#endif
