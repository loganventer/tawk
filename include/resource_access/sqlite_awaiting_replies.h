#ifndef APP_RESOURCE_ACCESS_SQLITE_AWAITING_REPLIES_H
#define APP_RESOURCE_ACCESS_SQLITE_AWAITING_REPLIES_H

#include <sqlite3.h>

#include "contracts/i_awaiting_replies.h"
#include "core/account_id.h"

IAwaitingReplies *sqlite_awaiting_replies_create(sqlite3 *db, AccountId account);

#endif
