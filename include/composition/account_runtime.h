#ifndef APP_COMPOSITION_ACCOUNT_RUNTIME_H
#define APP_COMPOSITION_ACCOUNT_RUNTIME_H

#include "clients/account_services.h"
#include "composition/account_runtime_params.h"
#include "core/account.h"

/* Everything that runs for one account: its gateway and event queue, its
 * stores over the shared database, and the managers that use them. Part of
 * the composition root: with main.c, the only place that makes these. */
typedef struct AccountRuntime AccountRuntime;

AccountRuntime        *account_runtime_create(const AccountRuntimeParams *params, const Account *account);
/* Stops the backend and takes everything down, in reverse order. */
void                   account_runtime_destroy(AccountRuntime *runtime);
const AccountServices *account_runtime_services(const AccountRuntime *runtime);
void                   account_runtime_set_label(AccountRuntime *runtime, const char *label);

/* Where an account keeps its login: the first account's stays where it
 * always was, the others get a folder of their own. */
void account_runtime_auth_dir(const Settings *settings, AccountId id, char *out, size_t size);

#endif
