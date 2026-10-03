#ifndef APP_RESOURCE_ACCESS_SQLITE_CHAT_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_CHAT_STORE_H

#include <sqlite3.h>

#include "contracts/i_chat_store.h"
#include "core/account_id.h"

/* A store over the chats of one account. */
IChatStore *sqlite_chat_store_create(sqlite3 *db, AccountId account);

#endif
