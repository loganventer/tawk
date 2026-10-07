#ifndef APP_CORE_PRESENCE_STATE_H
#define APP_CORE_PRESENCE_STATE_H

/* Whether a contact is on WhatsApp right now, as far as they let you see. */
typedef enum PresenceState {
    PRESENCE_UNKNOWN = 0,   /* nothing heard, or they do not share it */
    PRESENCE_ONLINE,
    PRESENCE_OFFLINE
} PresenceState;

/* "online", "offline", or "" when unknown. */
const char   *presence_state_name(PresenceState state);
PresenceState presence_state_parse(const char *name);

#endif
