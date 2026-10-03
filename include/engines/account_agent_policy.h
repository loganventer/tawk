#ifndef APP_ENGINES_ACCOUNT_AGENT_POLICY_H
#define APP_ENGINES_ACCOUNT_AGENT_POLICY_H

#include "core/account.h"
#include "core/settings.h"

/* What agents may do with one account, with "follow" read out of the settings. Never returns follow. */
AccountAgentAccess account_agent_policy_access(const Account *account, const Settings *settings);
/* Whether agents see the account at all: any level but off. */
int account_agent_policy_visible(const Account *account, const Settings *settings);
/* The chats an agent may answer by itself in for the account: its own list,
 * or, for an account that follows the settings and has none, the list the
 * settings held before accounts had their own. */
const char *account_agent_policy_self_chats(const Account *account, const char *own_chats, const Settings *settings);

#endif
