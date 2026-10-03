#ifndef APP_CLIENTS_TUI_UNIFIED_CHAT_RULES_H
#define APP_CLIENTS_TUI_UNIFIED_CHAT_RULES_H

#include "core/account_id.h"
#include "core/chat_prefs.h"

/* What decides how several accounts' chats are listed together. */
typedef struct UnifiedChatRules {
    AccountId only;                 /* list this account alone; ACCOUNT_ID_NONE lists them all */
    int       merge_setting;        /* [chats] merge_accounts */
    AccountId primary;              /* your primary account */
    /* What was chosen for one contact; asked only for a contact that is on
     * more than one account. */
    void    (*prefs)(void *ctx, const char *jid, ChatPrefs *out);
    void     *ctx;
} UnifiedChatRules;

#endif
