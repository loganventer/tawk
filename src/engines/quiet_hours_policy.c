#include "engines/quiet_hours_policy.h"

#include <ctype.h>

/* A time of day from `*p`, as minutes; -1 when there is none. Moves `*p` past it. */
static int time_of_day(const char **p) {
    const char *s = *p;
    while (isspace((unsigned char)*s)) s++;
    if (!isdigit((unsigned char)*s)) return -1;
    int hour = 0, minute = 0, digits = 0;
    while (isdigit((unsigned char)*s) && digits < 2) { hour = hour * 10 + (*s++ - '0'); digits++; }
    if (*s == ':' || *s == 'h' || *s == '.') {
        s++;
        if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1])) return -1;
        minute = (s[0] - '0') * 10 + (s[1] - '0');
        s += 2;
    }
    if (isdigit((unsigned char)*s) || hour > 24 || minute > 59 || (hour == 24 && minute > 0)) return -1;
    while (isspace((unsigned char)*s)) s++;
    *p = s;
    return (hour % 24) * 60 + minute;
}

QuietHours quiet_hours_policy_parse(const char *text) {
    QuietHours none = { 0, 0, 0 };
    if (!text) return none;
    const char *p = text;
    int from = time_of_day(&p);
    if (from < 0 || *p != '-') return none;
    p++;
    int until = time_of_day(&p);
    if (until < 0 || *p != '\0' || from == until) return none;
    QuietHours hours = { 1, from, until };
    return hours;
}

int quiet_hours_policy_covers(QuietHours h, int minute) {
    if (!h.set) return 0;
    if (h.from < h.until) return minute >= h.from && minute < h.until;
    return minute >= h.from || minute < h.until;          /* over midnight */
}

int quiet_hours_policy_quiet(const char *weekdays, const char *weekend, int weekday, int minute) {
    QuietHours hours = quiet_hours_policy_parse(weekdays);
    if (weekday == 0 || weekday == 6) {
        QuietHours own = quiet_hours_policy_parse(weekend);
        if (own.set) hours = own;
    }
    return quiet_hours_policy_covers(hours, minute);
}
