#include "engines/account_agent_policy.h"

AccountAgentAccess account_agent_policy_access(const Account *account, const Settings *settings) {
    if (account->agent_access != ACCOUNT_AGENT_FOLLOW) return account->agent_access;
    AccountAgentAccess followed = account_agent_access_parse(settings->automation_access);
    /* The setting names read, send, manage or admin; anything else it holds reads as read, as it always did. */
    return followed == ACCOUNT_AGENT_OFF || followed == ACCOUNT_AGENT_FOLLOW ? ACCOUNT_AGENT_READ : followed;
}

int account_agent_policy_visible(const Account *account, const Settings *settings) {
    return account_agent_policy_access(account, settings) != ACCOUNT_AGENT_OFF;
}

const char *account_agent_policy_self_chats(const Account *account, const char *own_chats, const Settings *settings) {
    if (own_chats && own_chats[0]) return own_chats;
    return account->agent_access == ACCOUNT_AGENT_FOLLOW ? settings->automation_self_chats : "";
}
