#include "engines/owner_message_rule.h"

OwnerMessageKind owner_message_rule_kind(const Message *msg, int sent_by_tawk) {
    if (!msg || sent_by_tawk || !msg->from_me || msg->deleted) return OWNER_MESSAGE_NOT;
    if (msg->type != MESSAGE_TYPE_TEXT || !msg->text || !msg->text[0]) return OWNER_MESSAGE_DATA;
    if (msg->forwarded || msg->quoted_id[0]) return OWNER_MESSAGE_DATA;
    return OWNER_MESSAGE_WORDS;
}
