/* The line under a chat's name: how "online" and "last seen" read, and when
 * the about text shows in their place. */
#include "clients/tui/chat_subtitle.h"
#include "core/chat.h"
#include "core/settings.h"
#include "engines/presence_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define NOW        1791374400   /* Wed 7 Oct 2026 12:00 UTC */
#define THIS_MORN  1791363900   /* 09:05 the same day */
#define LAST_NIGHT 1791311400   /* Tue 6 Oct 18:30 */
#define SATURDAY   1791011700   /* Sat 3 Oct 07:15 */
#define LONG_AGO   1789898400   /* 20 Sep 10:00 */

static int reads(PresenceState state, int64_t last_seen, int use_24h, const char *want) {
    char out[64] = "x";
    presence_text_format(state, last_seen, NOW, use_24h, out, sizeof(out));
    if (strcmp(out, want) != 0) fprintf(stderr, "  got \"%s\", wanted \"%s\"\n", out, want);
    return strcmp(out, want) == 0;
}

static void test_text(void) {
    CHECK(reads(PRESENCE_ONLINE, 0, 1, "online") && reads(PRESENCE_ONLINE, THIS_MORN, 1, "online"), "someone who is here is online");
    CHECK(reads(PRESENCE_OFFLINE, THIS_MORN, 1, "last seen today at 9:05"), "earlier today gives the time");
    CHECK(reads(PRESENCE_OFFLINE, THIS_MORN, 0, "last seen today at 9:05 AM"), "in the clock the user chose");
    CHECK(reads(PRESENCE_OFFLINE, LAST_NIGHT, 1, "last seen yesterday at 18:30"), "yesterday says so");
    CHECK(reads(PRESENCE_OFFLINE, SATURDAY, 1, "last seen Saturday at 7:15"), "within a week names the day");
    CHECK(reads(PRESENCE_OFFLINE, LONG_AGO, 1, "last seen 20 Sep"), "longer ago gives the date");
    CHECK(reads(PRESENCE_OFFLINE, 0, 1, ""), "offline without a time says nothing");
    CHECK(reads(PRESENCE_UNKNOWN, 0, 1, "") && reads(PRESENCE_UNKNOWN, THIS_MORN, 1, ""), "and so does someone we know nothing about");
}

static int subtitle(const Settings *s, const Chat *c, const char *want) {
    char out[96] = "x";
    chat_subtitle_text(s, c, "Hey there! I am using WhatsApp.", NOW, out, sizeof(out));
    if (strcmp(out, want) != 0) fprintf(stderr, "  got \"%s\", wanted \"%s\"\n", out, want);
    return strcmp(out, want) == 0;
}

static void test_subtitle(void) {
    Settings s;
    settings_set_defaults(&s);
    s.use_24h_clock = 1;
    Chat mom;
    chat_init(&mom, "27820000001@s.whatsapp.net");
    CHECK(s.show_online == 1, "showing who is online is on unless switched off");
    CHECK(subtitle(&s, &mom, "Hey there! I am using WhatsApp."), "with nothing known the about text shows, as before");
    mom.presence = PRESENCE_ONLINE;
    CHECK(subtitle(&s, &mom, "online"), "someone online says so under their name");
    mom.presence = PRESENCE_OFFLINE;
    mom.last_seen = LAST_NIGHT;
    CHECK(subtitle(&s, &mom, "last seen yesterday at 18:30"), "and someone who left says when");
    mom.last_seen = 0;
    CHECK(subtitle(&s, &mom, "Hey there! I am using WhatsApp."), "offline without a time falls back to the about text");
    mom.presence = PRESENCE_ONLINE;
    s.show_online = 0;
    CHECK(subtitle(&s, &mom, "Hey there! I am using WhatsApp."), "switched off, it is the about text again");
    s.show_online = 1;
    mom.soft_locked = 1;
    CHECK(subtitle(&s, &mom, ""), "a soft-locked chat shows nothing at all");
    Chat family;
    chat_init(&family, "120363000000000001@g.us");
    family.presence = PRESENCE_ONLINE;
    CHECK(subtitle(&s, &family, "Hey there! I am using WhatsApp."), "a group is never online");
    char out[8] = "x";
    chat_subtitle_text(&s, NULL, "about", NOW, out, sizeof(out));
    CHECK(out[0] == '\0', "no chat, no line");
}

int main(void) {
    setenv("TZ", "UTC", 1);
    tzset();
    test_text();
    test_subtitle();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: an open chat says who is online or when they were last seen, unless you switch it off\n");
    return 0;
}
