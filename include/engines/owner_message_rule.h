#ifndef APP_ENGINES_OWNER_MESSAGE_RULE_H
#define APP_ENGINES_OWNER_MESSAGE_RULE_H

#include "core/message.h"
#include "core/owner_message_kind.h"

/* What a message in the owner's chat is to an agent. `sent_by_tawk` says
 * whether tawk put it there itself, which is the only way an agent's answer
 * is told from yours: both carry your number. Only text you typed is your
 * words; anything forwarded, quoted, or not text is passed on as data. */
OwnerMessageKind owner_message_rule_kind(const Message *msg, int sent_by_tawk);

#endif
