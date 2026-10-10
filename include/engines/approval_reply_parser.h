#ifndef APP_ENGINES_APPROVAL_REPLY_PARSER_H
#define APP_ENGINES_APPROVAL_REPLY_PARSER_H

#include "core/approval_reply.h"

/* What a quoted answer to a request's card says: "y", "yes", "ok", "ja" or a
 * thumbs up allows it, "n", "no", "nee" or a thumbs down declines it, in any
 * case and with a full stop or not. Any other words are the text to send instead. */
ApprovalReply approval_reply_parse(const char *text);
/* What a reaction on the card says: a thumbs up or a tick allows, a thumbs
 * down or a cross declines, anything else answers nothing. */
ApprovalReply approval_reply_reaction(const char *emoji);

#endif
