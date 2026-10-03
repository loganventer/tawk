#include "core/account_agent_access.h"

#include <string.h>

static const char *const NAMES[ACCOUNT_AGENT_ACCESS_COUNT] = { "off", "follow", "read", "send", "manage", "admin" };

const char *account_agent_access_name(AccountAgentAccess access) {
    return access >= 0 && access < ACCOUNT_AGENT_ACCESS_COUNT ? NAMES[access] : NAMES[ACCOUNT_AGENT_OFF];
}

AccountAgentAccess account_agent_access_parse(const char *name) {
    for (int i = 0; name && i < ACCOUNT_AGENT_ACCESS_COUNT; i++) {
        if (strcmp(name, NAMES[i]) == 0) return (AccountAgentAccess)i;
    }
    return ACCOUNT_AGENT_OFF;
}
