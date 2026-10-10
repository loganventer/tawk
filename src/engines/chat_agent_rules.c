#include "engines/chat_agent_rules.h"

int chat_agent_rules_readable(ChatAgentRule rule) { return rule != CHAT_AGENT_HIDDEN; }

int chat_agent_rules_writable(ChatAgentRule rule) { return rule == CHAT_AGENT_FOLLOW || rule == CHAT_AGENT_ALWAYS_ASK; }

int chat_agent_rules_always_asks(ChatAgentRule rule) { return rule == CHAT_AGENT_ALWAYS_ASK; }

ChatAgentRule chat_agent_rules_next(ChatAgentRule rule) {
    switch (rule) {
        case CHAT_AGENT_FOLLOW:     return CHAT_AGENT_ALWAYS_ASK;
        case CHAT_AGENT_ALWAYS_ASK: return CHAT_AGENT_NO_SEND;
        case CHAT_AGENT_NO_SEND:    return CHAT_AGENT_HIDDEN;
        default:                    return CHAT_AGENT_FOLLOW;
    }
}

const char *chat_agent_rules_label(ChatAgentRule rule) {
    switch (rule) {
        case CHAT_AGENT_ALWAYS_ASK: return "always ask me";
        case CHAT_AGENT_NO_SEND:    return "read only";
        case CHAT_AGENT_HIDDEN:     return "hidden from agents";
        default:                    return "as the account says";
    }
}

ChatAgentRule chat_agent_rules_from(int stored) {
    return stored == CHAT_AGENT_ALWAYS_ASK || stored == CHAT_AGENT_NO_SEND || stored == CHAT_AGENT_HIDDEN
               ? (ChatAgentRule)stored : CHAT_AGENT_FOLLOW;
}
