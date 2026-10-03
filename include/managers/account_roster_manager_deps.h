#ifndef APP_MANAGERS_ACCOUNT_ROSTER_MANAGER_DEPS_H
#define APP_MANAGERS_ACCOUNT_ROSTER_MANAGER_DEPS_H

#include "contracts/i_account_store.h"
#include "contracts/i_chat_prefs_store.h"

/* What the account roster manager depends on, injected by the composition root. */
typedef struct AccountRosterManagerDeps {
    IAccountStore   *accounts;
    IChatPrefsStore *prefs;
} AccountRosterManagerDeps;

#endif
