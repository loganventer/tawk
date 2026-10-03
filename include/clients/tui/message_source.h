#ifndef APP_CLIENTS_TUI_MESSAGE_SOURCE_H
#define APP_CLIENTS_TUI_MESSAGE_SOURCE_H

#include "core/account_id.h"
#include "core/message.h"

/* One account's messages of the open chat, oldest first, as its messaging manager holds them. */
typedef struct MessageSource {
    AccountId      account;
    const Message *messages;
    int            count;
} MessageSource;

#endif
