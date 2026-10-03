#ifndef APP_CONTRACTS_I_CHAT_PREFS_STORE_H
#define APP_CONTRACTS_I_CHAT_PREFS_STORE_H

#include "core/chat_prefs.h"

/* What you chose for a person or group, across your accounts. */
typedef struct IChatPrefsStore {
    void *ctx;
    /* Always answers: someone with nothing chosen gets the defaults. */
    int  (*get)(struct IChatPrefsStore *self, const char *jid, ChatPrefs *out);
    int  (*set_send_account)(struct IChatPrefsStore *self, const char *jid, AccountId account);
    int  (*set_merge)(struct IChatPrefsStore *self, const char *jid, ChatMergeChoice merge);
    /* Everyone with a sending account of their own. Returns how many, or -1. */
    int  (*list_send_accounts)(struct IChatPrefsStore *self, ChatPrefs *out, int max);
    /* Puts everyone who sent from `account` back on the primary one. */
    int  (*forget_account)(struct IChatPrefsStore *self, AccountId account);
    /* The same person under another address. */
    int  (*reassign_jid)(struct IChatPrefsStore *self, const char *from, const char *to);
    void (*destroy)(struct IChatPrefsStore *self);
} IChatPrefsStore;

#endif
