#ifndef APP_CONTRACTS_I_SENT_ID_LOG_H
#define APP_CONTRACTS_I_SENT_ID_LOG_H

#include <stdint.h>

#include "core/account_id.h"
#include "core/sent_kind.h"

/* The ids of the messages tawk itself put into the owner's chat, kept so
 * that what is left in that chat is known to be yours, after a restart too. */
typedef struct ISentIdLog {
    void *ctx;
    int  (*note)(struct ISentIdLog *self, AccountId account, const char *message_id, SentKind kind, int ref, int64_t at);
    /* 0 when tawk sent it, with what it was; -1 when it did not. */
    int  (*find)(struct ISentIdLog *self, AccountId account, const char *message_id, SentKind *kind, int *ref);
    /* Forgets what was sent before `before`. */
    int  (*prune)(struct ISentIdLog *self, int64_t before);
    void (*destroy)(struct ISentIdLog *self);
} ISentIdLog;

#endif
