#ifndef APP_MANAGERS_OWNER_CHAT_MANAGER_DEPS_H
#define APP_MANAGERS_OWNER_CHAT_MANAGER_DEPS_H

#include "contracts/i_sent_id_log.h"
#include "core/settings.h"

typedef struct OwnerChatManagerDeps {
    ISentIdLog     *sent;
    const Settings *settings;
} OwnerChatManagerDeps;

#endif
