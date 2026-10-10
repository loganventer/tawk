#ifndef APP_ENGINES_REMINDER_RULE_H
#define APP_ENGINES_REMINDER_RULE_H

#include <stdint.h>

#include "core/chat_reminder.h"

/* Reads what follows /remind: a time as /later takes it ("9:00",
 * "tomorrow", "+2h", "fri 17:30"), or "reply" for no time at all. Sets
 * *due (0 for "reply") and returns 0; -1 when it is neither or the time is past. */
int reminder_rule_parse(const char *text, int64_t now, int64_t *due);
/* Whether a chat comes back now: its time has come, or its person wrote. */
int reminder_rule_due(const ChatReminder *reminder, int64_t now, int unread);
/* The newest time a message of yours may have to count as unanswered for `days` days. */
int64_t reminder_rule_awaiting_before(int64_t now, int days);

#endif
