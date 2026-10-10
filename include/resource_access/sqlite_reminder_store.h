#ifndef APP_RESOURCE_ACCESS_SQLITE_REMINDER_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_REMINDER_STORE_H

#include <sqlite3.h>

#include "contracts/i_reminder_store.h"

IReminderStore *sqlite_reminder_store_create(sqlite3 *db);

#endif
