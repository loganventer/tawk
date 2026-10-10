#ifndef APP_ENGINES_OWNER_REPLY_RULE_H
#define APP_ENGINES_OWNER_REPLY_RULE_H

#include <stdint.h>

/* Whether one more of an agent's answers may go out unasked in the owner's
 * chat: `sent_ms` holds when the last `count` went, `per_hour` is the limit. */
int owner_reply_rule_room(const int64_t *sent_ms, int count, int per_hour, int64_t now_ms);
/* Whether an operation is one that may go out there unasked: a plain message, nothing else. */
int owner_reply_rule_covers(const char *op);

#endif
