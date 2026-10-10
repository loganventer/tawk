#ifndef APP_ENGINES_TRANSCRIPT_VALIDATOR_H
#define APP_ENGINES_TRANSCRIPT_VALIDATOR_H

#include <stddef.h>

#include "core/message.h"

/* Whether a transcript may be kept for a message. Returns NULL when it may,
 * or a sentence saying why not: only a voice note or other audio that was
 * not deleted has one, the text must be there and within TRANSCRIPT_MAX_BYTES,
 * and the language is a short code or empty. */
const char *transcript_validator_refusal(const Message *message, const char *language, const char *text);
/* Makes text from a transcriber safe to show: control characters become
 * spaces, line breaks are kept, and the ends are trimmed. In place. */
void        transcript_validator_clean(char *text);
/* The language as it is stored: lower case, or "" for "auto" and for none. */
void        transcript_validator_language(const char *language, char *out, size_t size);

#endif
