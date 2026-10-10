#ifndef APP_CONTRACTS_I_CHAT_SUMMARY_PREFS_H
#define APP_CONTRACTS_I_CHAT_SUMMARY_PREFS_H

/* Whether one person or group is in TL;DR mode. It is read with the rest of
 * ChatPrefs, through IChatPrefsStore; this sets it. Handed out by the chat
 * prefs store, which owns it. */
typedef struct IChatSummaryPrefs {
    void *ctx;
    int  (*set_tldr)(struct IChatSummaryPrefs *self, const char *jid, int on);
} IChatSummaryPrefs;

#endif
