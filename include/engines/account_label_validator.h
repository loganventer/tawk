#ifndef APP_ENGINES_ACCOUNT_LABEL_VALIDATOR_H
#define APP_ENGINES_ACCOUNT_LABEL_VALIDATOR_H

#include <stddef.h>

#define ACCOUNT_LABEL_MAX_CHARS 24

/* 0 when `label` (UTF-8) may name an account: 1 to 24 characters, no line
 * breaks, commas or other control characters, and not only spaces.
 * Otherwise -1 and a reason for people in `why`. */
int account_label_validate(const char *label, char *why, size_t why_size);
/* Whether two labels are the same to a person: case and the spaces around them do not count. */
int account_label_same(const char *a, const char *b);

#endif
