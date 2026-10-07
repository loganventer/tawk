#include "core/presence_state.h"

#include <string.h>

const char *presence_state_name(PresenceState state) {
    switch (state) {
        case PRESENCE_ONLINE:  return "online";
        case PRESENCE_OFFLINE: return "offline";
        default:               return "";
    }
}

PresenceState presence_state_parse(const char *name) {
    if (!name) return PRESENCE_UNKNOWN;
    if (strcmp(name, "online") == 0) return PRESENCE_ONLINE;
    if (strcmp(name, "offline") == 0) return PRESENCE_OFFLINE;
    return PRESENCE_UNKNOWN;
}
