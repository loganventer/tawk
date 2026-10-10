#ifndef APP_ENGINES_IDLE_LOCK_POLICY_H
#define APP_ENGINES_IDLE_LOCK_POLICY_H

#include <stdint.h>

/* The lock that asks for the passphrase of your chats. There is one only
 * when the chats are encrypted: otherwise there is no secret to ask for. */
int idle_lock_policy_available(int encrypted);
/* Whether to lock by itself: you set a number of minutes, and that long has passed without a key. */
int idle_lock_policy_due(int encrypted, int lock_minutes, int64_t idle_ms);
/* How long to wait before the next try after `failed` wrong ones: none for
 * the first three, then one second doubling up to a minute. */
int64_t idle_lock_policy_wait_ms(int failed);

#endif
