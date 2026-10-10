#include "engines/remote_approval_policy.h"

int remote_approval_policy_offers(WriteKind kind, int new_chat, int chat_visible, int about_owner_chat) {
    return kind == WRITE_KIND_SEND && !new_chat && chat_visible && !about_owner_chat;
}

int remote_approval_policy_due(int64_t asked_ms, int64_t now_ms, int64_t wait_ms) {
    return now_ms - asked_ms >= wait_ms;
}
