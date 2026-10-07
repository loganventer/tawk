#include "engines/presence_text.h"

#include <stdio.h>
#include <time.h>

#include "utilities/clock_util.h"

static void last_seen_text(int64_t last_seen, int64_t now, int use_24h, char *out, size_t size) {
    char at[24];
    clock_format_time(last_seen, use_24h, at, sizeof(at));
    int64_t days = clock_local_day(now) - clock_local_day(last_seen);
    time_t t = (time_t)last_seen;
    struct tm tm_v;
    localtime_r(&t, &tm_v);
    if (days <= 0) {
        snprintf(out, size, "last seen today at %s", at);
    } else if (days == 1) {
        snprintf(out, size, "last seen yesterday at %s", at);
    } else if (days < 7) {
        char day[24];
        strftime(day, sizeof(day), "%A", &tm_v);
        snprintf(out, size, "last seen %s at %s", day, at);
    } else {
        char month[8];
        strftime(month, sizeof(month), "%b", &tm_v);
        snprintf(out, size, "last seen %d %s", tm_v.tm_mday, month);
    }
}

void presence_text_format(PresenceState state, int64_t last_seen, int64_t now, int use_24h, char *out, size_t size) {
    if (!out || size == 0) return;
    out[0] = '\0';
    if (state == PRESENCE_ONLINE) snprintf(out, size, "online");
    else if (state == PRESENCE_OFFLINE && last_seen > 0) last_seen_text(last_seen, now, use_24h, out, size);
}
