#ifndef APP_ENGINES_APPROVAL_CARD_TEXT_H
#define APP_ENGINES_APPROVAL_CARD_TEXT_H

#include <stddef.h>

#include "core/approval_card.h"

/* The message that puts a waiting request to you in the owner's chat: who
 * asks, what for, where, the exact words, and how to answer. The words are
 * cut with a mark when they would not fit; a request cut that way can still
 * be allowed, and tawk's own window shows all of it. */
void approval_card_text(const ApprovalCard *card, char *out, size_t size);

#endif
