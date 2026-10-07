#include "engines/presence_tracker.h"

#include <string.h>

#include "utilities/str_util.h"

void presence_tracker_init(PresenceTracker *tracker) {
    memset(tracker, 0, sizeof(*tracker));
}

static ContactPresence *slot_of(PresenceTracker *tracker, const char *jid) {
    for (int i = 0; i < tracker->count; i++) {
        if (strcmp(tracker->items[i].jid, jid) == 0) return &tracker->items[i];
    }
    return NULL;
}

static ContactPresence *new_slot(PresenceTracker *tracker, const char *jid) {
    ContactPresence *p;
    if (tracker->count < PRESENCE_TRACKER_SIZE) {
        p = &tracker->items[tracker->count++];
    } else {
        p = &tracker->items[tracker->next];
        tracker->next = (tracker->next + 1) % PRESENCE_TRACKER_SIZE;
    }
    memset(p, 0, sizeof(*p));
    str_copy(p->jid, sizeof(p->jid), jid);
    return p;
}

int presence_tracker_note(PresenceTracker *tracker, const char *jid, PresenceState state, int64_t last_seen) {
    if (!jid || !jid[0] || state == PRESENCE_UNKNOWN) return 0;
    ContactPresence *p = slot_of(tracker, jid);
    if (!p) p = new_slot(tracker, jid);
    int changed = p->state != state;
    p->state = state;
    if (last_seen > 0) p->last_seen = last_seen;
    return changed;
}

const ContactPresence *presence_tracker_find(const PresenceTracker *tracker, const char *jid) {
    if (!jid || !jid[0]) return NULL;
    for (int i = 0; i < tracker->count; i++) {
        if (strcmp(tracker->items[i].jid, jid) == 0) return &tracker->items[i];
    }
    return NULL;
}

int presence_tracker_reset(PresenceTracker *tracker) {
    int had = tracker->count > 0;
    presence_tracker_init(tracker);
    return had;
}
