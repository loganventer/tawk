#ifndef APP_RESOURCE_ACCESS_SQLITE_SUMMARY_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_SUMMARY_STORE_H

#include <sqlite3.h>

#include "contracts/i_summary_store.h"
#include "core/account_id.h"

/* A store over the TL;DR summaries of one account's messages. */
ISummaryStore *sqlite_summary_store_create(sqlite3 *db, AccountId account);

#endif
