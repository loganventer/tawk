#ifndef APP_ENGINES_PRESENCE_TRACKER_H
#define APP_ENGINES_PRESENCE_TRACKER_H

#include <stdint.h>

#include "core/contact_presence.h"

#define PRESENCE_TRACKER_SIZE 64

/* Who is online, for the few contacts WhatsApp tells us about (the ones whose
 * chat was opened). When it is full the contact heard from longest ago makes
 * room. */
typedef struct PresenceTracker {
    ContactPresence items[PRESENCE_TRACKER_SIZE];
    int             count;
    int             next;       /* the slot to reuse once it is full */
} PresenceTracker;

void presence_tracker_init(PresenceTracker *tracker);
/* Remembers what was heard about `jid`. Returns 1 when their state changed
 * (came online, or left), 0 when it only confirms what was known. */
int  presence_tracker_note(PresenceTracker *tracker, const char *jid, PresenceState state, int64_t last_seen);
/* What is known about `jid`, or NULL. */
const ContactPresence *presence_tracker_find(const PresenceTracker *tracker, const char *jid);
/* Forgets everyone: nothing is known while we are offline ourselves. Returns 1 when anything was forgotten. */
int  presence_tracker_reset(PresenceTracker *tracker);

#endif
