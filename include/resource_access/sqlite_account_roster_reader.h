#ifndef APP_RESOURCE_ACCESS_SQLITE_ACCOUNT_ROSTER_READER_H
#define APP_RESOURCE_ACCESS_SQLITE_ACCOUNT_ROSTER_READER_H

#include "core/account.h"

/* Reads the accounts out of the database at `path` without changing it: the
 * file is opened read-only and is not upgraded. Returns how many were
 * written, or -1 when there is no database, it is encrypted, or it is from
 * before accounts. */
int sqlite_account_roster_read(const char *path, Account *out, int max);

#endif
