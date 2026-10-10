#ifndef APP_RESOURCE_ACCESS_SQLITE_SENT_ID_LOG_H
#define APP_RESOURCE_ACCESS_SQLITE_SENT_ID_LOG_H

#include <sqlite3.h>

#include "contracts/i_sent_id_log.h"

ISentIdLog *sqlite_sent_id_log_create(sqlite3 *db);

#endif
