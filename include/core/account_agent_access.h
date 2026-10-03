#ifndef APP_CORE_ACCOUNT_AGENT_ACCESS_H
#define APP_CORE_ACCOUNT_AGENT_ACCESS_H

/* What programs acting for a model may do with one account. A new account
 * starts at off: agents neither see nor reach it until you say otherwise. */
typedef enum AccountAgentAccess {
    ACCOUNT_AGENT_OFF = 0,
    ACCOUNT_AGENT_FOLLOW,      /* whatever [automation] access says: how the first account keeps behaving as it did */
    ACCOUNT_AGENT_READ,
    ACCOUNT_AGENT_SEND,
    ACCOUNT_AGENT_MANAGE,
    ACCOUNT_AGENT_ADMIN,
    ACCOUNT_AGENT_ACCESS_COUNT
} AccountAgentAccess;

/* "off", "follow", "read", "send", "manage", "admin". */
const char        *account_agent_access_name(AccountAgentAccess access);
/* Anything unknown reads as off, the safe side. */
AccountAgentAccess account_agent_access_parse(const char *name);

#endif
