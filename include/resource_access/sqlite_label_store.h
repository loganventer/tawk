#ifndef APP_RESOURCE_ACCESS_SQLITE_LABEL_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_LABEL_STORE_H

#include <sqlite3.h>

#include "contracts/i_label_store.h"

ILabelStore *sqlite_label_store_create(sqlite3 *db);

#endif
