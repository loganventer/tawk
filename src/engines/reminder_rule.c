#include "engines/reminder_rule.h"
#include "engines/schedule_time_parser.h"

#include <ctype.h>
#include <stddef.h>
#include <strings.h>

int reminder_rule_parse(const char *text, int64_t now, int64_t *due) {
    if (!text) return -1;
    while (isspace((unsigned char)*text)) text++;
    if (strncasecmp(text, "reply", 5) == 0) {
        const char *rest = text + 5;
        while (isspace((unsigned char)*rest)) rest++;
        if (*rest) return -1;
        *due = 0;
        return 0;
    }
    const char *rest = NULL;
    if (schedule_time_parse(text, now, due, &rest) != 0) return -1;
    while (rest && isspace((unsigned char)*rest)) rest++;
    return rest && *rest ? -1 : 0;
}

int reminder_rule_due(const ChatReminder *r, int64_t now, int unread) {
    if (unread > 0) return 1;
    return r->due_at > 0 && now >= r->due_at;
}

int64_t reminder_rule_awaiting_before(int64_t now, int days) {
    if (days < 1) days = 1;
    return now - (int64_t)days * 24 * 60 * 60;
}
