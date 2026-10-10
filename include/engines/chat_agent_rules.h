#ifndef APP_ENGINES_CHAT_AGENT_RULES_H
#define APP_ENGINES_CHAT_AGENT_RULES_H

#include "core/chat_agent_rule.h"

/* What a chat's own rule means for an agent. */
int chat_agent_rules_readable(ChatAgentRule rule);       /* it may see the chat */
int chat_agent_rules_writable(ChatAgentRule rule);       /* it may ask to write there */
int chat_agent_rules_always_asks(ChatAgentRule rule);    /* each write is yours to answer, every time */
/* The next rule when you step through them, and the words for one. */
ChatAgentRule chat_agent_rules_next(ChatAgentRule rule);
const char   *chat_agent_rules_label(ChatAgentRule rule);
/* A stored number as a rule; anything unknown is "as the account says". */
ChatAgentRule chat_agent_rules_from(int stored);

#endif
