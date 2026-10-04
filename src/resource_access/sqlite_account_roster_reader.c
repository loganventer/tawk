#include "resource_access/sqlite_account_roster_reader.h"
#include "resource_access/sqlite_account_store.h"

#include <sqlite3.h>

int sqlite_account_roster_read(const char *path, Account *out, int max) {
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    int n = -1;
    IAccountStore *store = sqlite_account_store_create(db);
    if (store) {
        n = store->list(store, out, max);          /* fails on an encrypted file or one with no accounts table */
        store->destroy(store);
    }
    sqlite3_close(db);
    return n;
}
