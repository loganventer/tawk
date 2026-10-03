#ifndef APP_CLIENTS_TUI_UNIFIED_CHAT_LIST_H
#define APP_CLIENTS_TUI_UNIFIED_CHAT_LIST_H

#include "clients/tui/chat_source.h"
#include "clients/tui/unified_chat_rules.h"

/* The chats of every running account as one list. A contact who is on
 * several accounts is one row when their chats merge, and one row for each
 * account when they do not. Each row says which accounts have the chat and
 * which one it acts through. */
typedef struct UnifiedChatList {
    Chat *rows;
    int   count;
    int   capacity;
} UnifiedChatList;

void unified_chat_list_init(UnifiedChatList *list);
void unified_chat_list_free(UnifiedChatList *list);
/* Builds the rows from `sources`, pinned first and then newest first. The
 * n-th source is bit n of a row's `accounts`. */
void unified_chat_list_build(UnifiedChatList *list, const ChatSource *sources, int source_count, const UnifiedChatRules *rules);
/* The row for `jid`, through `account` when it is not ACCOUNT_ID_NONE; NULL when there is none. */
const Chat *unified_chat_list_find(const UnifiedChatList *list, AccountId account, const char *jid);

#endif
