#ifndef APP_CORE_CONTACT_PRESENCE_H
#define APP_CORE_CONTACT_PRESENCE_H

#include <stdint.h>

#include "core/presence_state.h"

/* What is known about one contact being online. */
typedef struct ContactPresence {
    char          jid[128];
    PresenceState state;
    int64_t       last_seen;    /* epoch seconds, 0 when they do not share it */
} ContactPresence;

#endif
