#ifndef APP_ENGINES_QUIET_HOURS_POLICY_H
#define APP_ENGINES_QUIET_HOURS_POLICY_H

#include "core/quiet_hours.h"

/* Reads "22:00-07:00" (or "22:00 - 7:00", "22-7"). Anything else, the empty
 * string and a stretch of no length give none. */
QuietHours quiet_hours_policy_parse(const char *text);
/* Whether minute `minute_of_day` falls inside. */
int quiet_hours_policy_covers(QuietHours hours, int minute_of_day);
/* Whether it is quiet now: on Saturday and Sunday (`weekday` 6 and 0) the
 * weekend's hours apply when there are any, else the weekdays' do. */
int quiet_hours_policy_quiet(const char *weekdays, const char *weekend, int weekday, int minute_of_day);

#endif
