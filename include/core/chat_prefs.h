#ifndef APP_CORE_CHAT_PREFS_H
#define APP_CORE_CHAT_PREFS_H

#include "core/account_id.h"
#include "core/chat_merge_choice.h"

/* What you chose for a person or group, whichever of your accounts they are on. */
typedef struct ChatPrefs {
    char            jid[128];
    AccountId       send_account;   /* ACCOUNT_ID_NONE: the primary account */
    ChatMergeChoice merge;
} ChatPrefs;

#endif
