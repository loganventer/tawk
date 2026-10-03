#include "engines/chat_merge_policy.h"

int chat_merge_policy_merges(int setting_on, ChatMergeChoice choice) {
    if (choice == CHAT_MERGE_ALWAYS) return 1;
    if (choice == CHAT_MERGE_NEVER) return 0;
    return setting_on != 0;
}
