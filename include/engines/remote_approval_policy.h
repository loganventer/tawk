#ifndef APP_ENGINES_REMOTE_APPROVAL_POLICY_H
#define APP_ENGINES_REMOTE_APPROVAL_POLICY_H

#include <stdint.h>

#include "core/write_kind.h"

/* Whether a waiting request may be put to you on WhatsApp at all. Only
 * sends are: nothing that deletes, blocks or changes tawk or WhatsApp, no
 * first message to someone new, and nothing about the owner's chat itself. */
int remote_approval_policy_offers(WriteKind kind, int new_chat, int chat_visible, int about_owner_chat);
/* Whether it is time: it has waited `wait_ms` in tawk without an answer. */
int remote_approval_policy_due(int64_t asked_ms, int64_t now_ms, int64_t wait_ms);

#endif
