#ifndef APP_UTILITIES_PLAIN_TITLE_H
#define APP_UTILITIES_PLAIN_TITLE_H

#include <stddef.h>

/* A terminal title in plain ASCII, for a terminal that cannot be trusted with
 * more: the separator dot becomes a dash, an ellipsis three dots, and every
 * other character outside ASCII is left out. Spaces left behind are closed up. */
void plain_title(const char *title, char *out, size_t size);

#endif
