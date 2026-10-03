#ifndef APP_CLIENTS_I_ACCOUNT_DIRECTORY_H
#define APP_CLIENTS_I_ACCOUNT_DIRECTORY_H

#include "clients/account_services.h"

/* The accounts that are running, as the clients see them. The composition
 * root implements it: a client asks for an account's managers, or for an
 * account to be started or stopped, without knowing how one is put together. */
typedef struct IAccountDirectory {
    void *ctx;
    int  (*count)(struct IAccountDirectory *self);
    /* In id order; NULL past the end. */
    const AccountServices *(*at)(struct IAccountDirectory *self, int index);
    const AccountServices *(*find)(struct IAccountDirectory *self, AccountId id);
    /* Starts the account `id`, which is already in the roster. Returns its
     * managers, or NULL when it cannot be started. */
    const AccountServices *(*start)(struct IAccountDirectory *self, AccountId id);
    /* Stops the account and takes it down. Its rows are not touched. */
    int  (*stop)(struct IAccountDirectory *self, AccountId id);
    /* Stops it and removes the folder its login was kept in. The first
     * account's folder stays, as it is also where a new login would go. */
    int  (*forget)(struct IAccountDirectory *self, AccountId id);
    /* The label of `id` changed in the roster: what `label` points at follows. */
    void (*relabel)(struct IAccountDirectory *self, AccountId id);
} IAccountDirectory;

#endif
