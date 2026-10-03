#ifndef APP_CONTRACTS_I_ACCOUNT_STORE_H
#define APP_CONTRACTS_I_ACCOUNT_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "core/account.h"

/* Your accounts, and the few things kept for each that are not settings. */
typedef struct IAccountStore {
    void *ctx;
    /* In id order. Returns how many were written to `out`, or -1. */
    int  (*list)(struct IAccountStore *self, Account *out, int max);
    int  (*get)(struct IAccountStore *self, AccountId id, Account *out);
    /* The new account's id goes to id_out; it is not primary and agents may not use it. */
    int  (*add)(struct IAccountStore *self, const char *label, int colour, int64_t now, AccountId *id_out);
    int  (*rename)(struct IAccountStore *self, AccountId id, const char *label);
    /* Makes `id` the only primary account. */
    int  (*set_primary)(struct IAccountStore *self, AccountId id);
    int  (*set_agent_access)(struct IAccountStore *self, AccountId id, AccountAgentAccess access);
    /* Who the account turned out to be once it linked. */
    int  (*set_identity)(struct IAccountStore *self, AccountId id, const char *jid, const char *name);
    int  (*set_last_chat)(struct IAccountStore *self, AccountId id, const char *jid);
    int  (*get_last_chat)(struct IAccountStore *self, AccountId id, char *out, size_t size);
    /* The chats (JIDs, comma-separated, or *) an agent may answer its own requests in. */
    int  (*set_self_approval_chats)(struct IAccountStore *self, AccountId id, const char *chats);
    int  (*get_self_approval_chats)(struct IAccountStore *self, AccountId id, char *out, size_t size);
    /* Takes the account and everything kept for it out of the database. */
    int  (*remove)(struct IAccountStore *self, AccountId id);
    void (*destroy)(struct IAccountStore *self);
} IAccountStore;

#endif
