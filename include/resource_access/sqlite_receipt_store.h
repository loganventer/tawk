#ifndef APP_RESOURCE_ACCESS_SQLITE_RECEIPT_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_RECEIPT_STORE_H

#include <sqlite3.h>

#include "contracts/i_receipt_store.h"
#include "core/account_id.h"

/* A store over the receipts of one account's messages. */
IReceiptStore *sqlite_receipt_store_create(sqlite3 *db, AccountId account);

#endif
