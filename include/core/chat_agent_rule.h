#ifndef APP_CORE_CHAT_AGENT_RULE_H
#define APP_CORE_CHAT_AGENT_RULE_H

/* What you let agents do in one chat, on top of what the account allows.
 * A rule can only tighten: it never lets an agent do more than the account's level does. */
typedef enum {
    CHAT_AGENT_FOLLOW = 0,    /* as the account says */
    CHAT_AGENT_ALWAYS_ASK,    /* every send is asked about: no "for this session", no answering its own */
    CHAT_AGENT_NO_SEND,       /* agents may read here and never write */
    CHAT_AGENT_HIDDEN         /* agents do not see this chat at all */
} ChatAgentRule;

#endif
