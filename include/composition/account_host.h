#ifndef APP_COMPOSITION_ACCOUNT_HOST_H
#define APP_COMPOSITION_ACCOUNT_HOST_H

#include "clients/i_account_directory.h"
#include "composition/account_runtime_params.h"
#include "contracts/i_account_store.h"

/* Holds the runtime of every account that is running, and is the account
 * directory the clients are given. Part of the composition root. */
typedef struct AccountHost AccountHost;

/* Starts a runtime for each account in `accounts`. */
AccountHost       *account_host_create(const AccountRuntimeParams *params, IAccountStore *accounts);
void               account_host_destroy(AccountHost *host);
IAccountDirectory *account_host_directory(AccountHost *host);

#endif
