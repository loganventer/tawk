#ifndef APP_CONTRACTS_I_CHAT_AGENT_PREFS_H
#define APP_CONTRACTS_I_CHAT_AGENT_PREFS_H

#include "core/chat_agent_choice.h"

/* The rule for agents in one person's or group's chat. It is read with the
 * rest of ChatPrefs, through IChatPrefsStore; this sets it and lists the
 * chats that have one. Handed out by the chat prefs store, which owns it. */
typedef struct IChatAgentPrefs {
    void *ctx;
    int  (*set_rule)(struct IChatAgentPrefs *self, const char *jid, ChatAgentRule rule);
    /* Every chat with a rule other than "as the account says". Returns how many, or -1. */
    int  (*list)(struct IChatAgentPrefs *self, ChatAgentChoice *out, int max);
} IChatAgentPrefs;

#endif
