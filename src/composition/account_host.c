#include "composition/account_host.h"
#include "composition/account_runtime.h"
#include "utilities/log.h"

#include <stdlib.h>

struct AccountHost {
    AccountRuntimeParams params;
    IAccountStore       *accounts;
    IAccountDirectory    directory;
    AccountRuntime      *runtimes[ACCOUNT_MAX];     /* in id order */
    int                  count;
};

static AccountHost *host_of(IAccountDirectory *self) { return (AccountHost *)self->ctx; }

static int index_of(const AccountHost *h, AccountId id) {
    for (int i = 0; i < h->count; i++) {
        if (account_runtime_services(h->runtimes[i])->id == id) return i;
    }
    return -1;
}

static int dir_count(IAccountDirectory *self) { return host_of(self)->count; }

static const AccountServices *dir_at(IAccountDirectory *self, int index) {
    AccountHost *h = host_of(self);
    return index >= 0 && index < h->count ? account_runtime_services(h->runtimes[index]) : NULL;
}

static const AccountServices *dir_find(IAccountDirectory *self, AccountId id) {
    AccountHost *h = host_of(self);
    int i = index_of(h, id);
    return i >= 0 ? account_runtime_services(h->runtimes[i]) : NULL;
}

/* Puts a runtime in its place by id. */
static void insert(AccountHost *h, AccountRuntime *rt) {
    AccountId id = account_runtime_services(rt)->id;
    int at = h->count;
    while (at > 0 && account_runtime_services(h->runtimes[at - 1])->id > id) {
        h->runtimes[at] = h->runtimes[at - 1];
        at--;
    }
    h->runtimes[at] = rt;
    h->count++;
}

static const AccountServices *dir_start(IAccountDirectory *self, AccountId id) {
    AccountHost *h = host_of(self);
    int have = index_of(h, id);
    if (have >= 0) return account_runtime_services(h->runtimes[have]);
    Account account;
    if (h->count >= ACCOUNT_MAX || h->accounts->get(h->accounts, id, &account) != 0) return NULL;
    AccountRuntime *rt = account_runtime_create(&h->params, &account);
    if (!rt) return NULL;
    insert(h, rt);
    LOG_INFO("account %d started", id);
    return account_runtime_services(rt);
}

static int dir_stop(IAccountDirectory *self, AccountId id) {
    AccountHost *h = host_of(self);
    int at = index_of(h, id);
    if (at < 0) return -1;
    account_runtime_destroy(h->runtimes[at]);
    for (int i = at; i + 1 < h->count; i++) h->runtimes[i] = h->runtimes[i + 1];
    h->runtimes[--h->count] = NULL;
    LOG_INFO("account %d stopped", id);
    return 0;
}

static void dir_relabel(IAccountDirectory *self, AccountId id) {
    AccountHost *h = host_of(self);
    int at = index_of(h, id);
    Account account;
    if (at >= 0 && h->accounts->get(h->accounts, id, &account) == 0) account_runtime_set_label(h->runtimes[at], account.label);
}

AccountHost *account_host_create(const AccountRuntimeParams *params, IAccountStore *accounts) {
    AccountHost *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->params = *params;
    h->accounts = accounts;
    h->directory = (IAccountDirectory){ h, dir_count, dir_at, dir_find, dir_start, dir_stop, dir_relabel };
    Account all[ACCOUNT_MAX];
    int n = accounts->list(accounts, all, ACCOUNT_MAX);
    for (int i = 0; i < n; i++) {
        AccountRuntime *rt = account_runtime_create(&h->params, &all[i]);
        if (rt) insert(h, rt);
    }
    return h;
}

void account_host_destroy(AccountHost *host) {
    if (!host) return;
    for (int i = host->count - 1; i >= 0; i--) account_runtime_destroy(host->runtimes[i]);
    free(host);
}

IAccountDirectory *account_host_directory(AccountHost *host) {
    return &host->directory;
}
