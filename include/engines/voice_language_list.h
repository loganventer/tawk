#ifndef APP_ENGINES_VOICE_LANGUAGE_LIST_H
#define APP_ENGINES_VOICE_LANGUAGE_LIST_H

#include <stddef.h>

/* Makes a list of languages typed or chosen ("AF, en,af , xx") into the one
 * kept: known codes only, in lower case, each once, in the order given,
 * joined by commas ("af,en"). Returns how many it holds. */
int voice_language_list_clean(const char *list, char *out, size_t size);

#endif
