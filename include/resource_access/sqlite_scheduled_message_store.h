#ifndef APP_RESOURCE_ACCESS_SQLITE_SCHEDULED_MESSAGE_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_SCHEDULED_MESSAGE_STORE_H

#include <sqlite3.h>

#include "contracts/i_scheduled_message_store.h"
#include "core/account_id.h"

/* Scheduled messages in the scheduled_messages table (migration 12). Borrows the connection. */
/* A store over the messages one account is waiting to send. */
IScheduledMessageStore *sqlite_scheduled_message_store_create(sqlite3 *db, AccountId account);

#endif
