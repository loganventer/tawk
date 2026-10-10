#include "engines/owner_reply_rule.h"

#include <string.h>

#define HOUR_MS (60 * 60 * 1000)

int owner_reply_rule_room(const int64_t *sent_ms, int count, int per_hour, int64_t now_ms) {
    if (per_hour <= 0) return 0;
    int recent = 0;
    for (int i = 0; i < count; i++) if (now_ms - sent_ms[i] < HOUR_MS) recent++;
    return recent < per_hour;
}

int owner_reply_rule_covers(const char *op) {
    return op && strcmp(op, "send_message") == 0;
}
