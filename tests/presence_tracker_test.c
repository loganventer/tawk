/* Who is online: what the tracker remembers, what counts as a change, and
 * how the state is named on the wire. */
#include "core/presence_state.h"
#include "engines/presence_tracker.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define MOM "27820000001@s.whatsapp.net"
#define DAD "27820000002@s.whatsapp.net"

static void test_names(void) {
    CHECK(strcmp(presence_state_name(PRESENCE_ONLINE), "online") == 0 && strcmp(presence_state_name(PRESENCE_OFFLINE), "offline") == 0 &&
          presence_state_name(PRESENCE_UNKNOWN)[0] == '\0', "online and offline have a name, unknown has none");
    CHECK(presence_state_parse("online") == PRESENCE_ONLINE && presence_state_parse("offline") == PRESENCE_OFFLINE,
          "and the names read back");
    CHECK(presence_state_parse("composing") == PRESENCE_UNKNOWN && presence_state_parse("") == PRESENCE_UNKNOWN &&
          presence_state_parse(NULL) == PRESENCE_UNKNOWN, "anything else is unknown");
}

static void test_tracker(void) {
    PresenceTracker t;
    presence_tracker_init(&t);
    CHECK(presence_tracker_find(&t, MOM) == NULL, "nothing is known at first");
    CHECK(presence_tracker_note(&t, MOM, PRESENCE_ONLINE, 0) == 1, "coming online is a change");
    CHECK(presence_tracker_note(&t, MOM, PRESENCE_ONLINE, 0) == 0, "hearing it again is not");
    const ContactPresence *p = presence_tracker_find(&t, MOM);
    CHECK(p && p->state == PRESENCE_ONLINE && p->last_seen == 0, "she is online");
    CHECK(presence_tracker_note(&t, MOM, PRESENCE_OFFLINE, 1791363900) == 1, "leaving is a change");
    p = presence_tracker_find(&t, MOM);
    CHECK(p && p->state == PRESENCE_OFFLINE && p->last_seen == 1791363900, "and when she was last seen is kept");
    CHECK(presence_tracker_note(&t, MOM, PRESENCE_OFFLINE, 0) == 0 && presence_tracker_find(&t, MOM)->last_seen == 1791363900,
          "a notice without a time does not wipe the time that was known");
    CHECK(presence_tracker_note(&t, DAD, PRESENCE_UNKNOWN, 0) == 0 && presence_tracker_find(&t, DAD) == NULL,
          "an unknown state is not remembered");
    CHECK(presence_tracker_note(&t, "", PRESENCE_ONLINE, 0) == 0 && presence_tracker_note(&t, NULL, PRESENCE_ONLINE, 0) == 0,
          "nor is a notice about nobody");
    CHECK(presence_tracker_reset(&t) == 1 && presence_tracker_find(&t, MOM) == NULL, "a reset forgets everyone");
    CHECK(presence_tracker_reset(&t) == 0, "and says so only when there was someone to forget");
}

static void test_full(void) {
    PresenceTracker t;
    presence_tracker_init(&t);
    char jid[64];
    for (int i = 0; i < PRESENCE_TRACKER_SIZE + 3; i++) {
        snprintf(jid, sizeof(jid), "2782%07d@s.whatsapp.net", i);
        presence_tracker_note(&t, jid, PRESENCE_ONLINE, 0);
    }
    CHECK(t.count == PRESENCE_TRACKER_SIZE, "it never grows past its size");
    CHECK(presence_tracker_find(&t, "27820000000@s.whatsapp.net") == NULL, "the one heard from first made room");
    snprintf(jid, sizeof(jid), "2782%07d@s.whatsapp.net", PRESENCE_TRACKER_SIZE + 2);
    CHECK(presence_tracker_find(&t, jid) != NULL, "and the newest is known");
}

int main(void) {
    test_names();
    test_tracker();
    test_full();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: who is online is remembered per person, and only a change counts as news\n");
    return 0;
}
