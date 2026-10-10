#ifndef APP_CORE_CHAT_AGENT_CHOICE_H
#define APP_CORE_CHAT_AGENT_CHOICE_H

#include "core/chat_agent_rule.h"

/* One chat that has a rule of its own for agents. */
typedef struct ChatAgentChoice {
    char          jid[128];
    ChatAgentRule rule;
} ChatAgentChoice;

#endif
