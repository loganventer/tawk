#ifndef APP_CONTRACTS_I_CHAT_ALERT_PREFS_H
#define APP_CONTRACTS_I_CHAT_ALERT_PREFS_H

#include "core/chat_alert_level.h"

/* Which messages of one person's or group's chat alert you. It is read with
 * the rest of ChatPrefs, through IChatPrefsStore; this sets it. Handed out
 * by the chat prefs store, which owns it. */
typedef struct IChatAlertPrefs {
    void *ctx;
    int  (*set_level)(struct IChatAlertPrefs *self, const char *jid, ChatAlertLevel level);
} IChatAlertPrefs;

#endif
