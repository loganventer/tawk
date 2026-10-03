#ifndef APP_RESOURCE_ACCESS_SQLITE_CHAT_PREFS_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_CHAT_PREFS_STORE_H

#include <sqlite3.h>

#include "contracts/i_chat_prefs_store.h"

IChatPrefsStore *sqlite_chat_prefs_store_create(sqlite3 *db);

#endif
