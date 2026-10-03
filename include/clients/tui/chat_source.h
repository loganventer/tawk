#ifndef APP_CLIENTS_TUI_CHAT_SOURCE_H
#define APP_CLIENTS_TUI_CHAT_SOURCE_H

#include "core/account_id.h"
#include "core/chat.h"

/* One running account's chats, as its messaging manager lists them. */
typedef struct ChatSource {
    AccountId   account;
    const Chat *chats;
    int         count;
} ChatSource;

#endif
