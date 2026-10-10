#include "engines/idle_lock_policy.h"

int idle_lock_policy_available(int encrypted) { return encrypted != 0; }

int idle_lock_policy_due(int encrypted, int lock_minutes, int64_t idle_ms) {
    return encrypted && lock_minutes > 0 && idle_ms >= (int64_t)lock_minutes * 60000;
}

int64_t idle_lock_policy_wait_ms(int failed) {
    if (failed < 3) return 0;
    int64_t wait = 1000;
    for (int i = 3; i < failed && wait < 60000; i++) wait *= 2;
    return wait > 60000 ? 60000 : wait;
}
