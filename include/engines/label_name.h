#ifndef APP_ENGINES_LABEL_NAME_H
#define APP_ENGINES_LABEL_NAME_H

#include <stddef.h>

/* A label as it is kept: trimmed, in lower case, at most 24 characters, with
 * no commas, slashes or control characters. Returns 0, or -1 when nothing
 * usable is left or it is too long. */
int label_name_clean(const char *text, char *out, size_t size);

#endif
