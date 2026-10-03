#include "engines/reply_account_policy.h"

static int has_chat(const ReplyAccountCandidate *c, int count, AccountId id) {
    if (id == ACCOUNT_ID_NONE) return 0;
    for (int i = 0; i < count; i++) if (c[i].id == id) return c[i].has_chat;
    return 0;
}

AccountId reply_account_policy_choose(AccountId for_message, AccountId for_contact, AccountId primary,
                                      const ReplyAccountCandidate *candidates, int count) {
    if (has_chat(candidates, count, for_message)) return for_message;
    if (has_chat(candidates, count, for_contact)) return for_contact;
    if (has_chat(candidates, count, primary)) return primary;
    AccountId newest = ACCOUNT_ID_NONE;
    int64_t newest_ts = 0;
    for (int i = 0; i < count; i++) {
        if (!candidates[i].has_chat) continue;
        if (newest == ACCOUNT_ID_NONE || candidates[i].newest_ts > newest_ts) {
            newest = candidates[i].id;
            newest_ts = candidates[i].newest_ts;
        }
    }
    return newest;
}
