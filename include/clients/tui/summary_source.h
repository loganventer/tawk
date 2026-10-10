#ifndef APP_CLIENTS_TUI_SUMMARY_SOURCE_H
#define APP_CLIENTS_TUI_SUMMARY_SOURCE_H

#include "core/account_id.h"
#include "core/message.h"
#include "core/summary.h"

/* Where the conversation finds the TL;DR summary of a long message, without
 * knowing who keeps them or who writes them. `find` fills `out` (the caller
 * disposes it) and returns -1 when the message has none. `owner` is the
 * account the message belongs to in a chat merged across accounts,
 * ACCOUNT_ID_NONE anywhere else. */
typedef struct SummarySource {
    void *ctx;
    int  (*find)(void *ctx, const Message *message, AccountId owner, Summary *out);
} SummarySource;

#endif
