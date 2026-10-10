#ifndef APP_ENGINES_SUMMARY_VALIDATOR_H
#define APP_ENGINES_SUMMARY_VALIDATOR_H

#include "core/message.h"

/* Whether a summary may be kept for a message. Returns NULL when it may, or
 * a sentence saying why not: only a text message that was not deleted has
 * one, and the summary must be there and within SUMMARY_MAX_BYTES. */
const char *summary_validator_refusal(const Message *message, const char *text);
/* Makes a summary safe to show and one paragraph: control characters and
 * line breaks become spaces, runs of spaces become one, the ends are trimmed. In place. */
void        summary_validator_clean(char *text);

#endif
