#include "engines/label_name.h"

#include <ctype.h>
#include <string.h>

#define LABEL_CHARS 24

int label_name_clean(const char *text, char *out, size_t size) {
    if (!text || size == 0) return -1;
    while (isspace((unsigned char)*text)) text++;
    size_t used = 0;
    int chars = 0;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c == 127 || c == ',' || c == '/') return -1;
        if ((c & 0xC0) != 0x80) chars++;
        if (chars > LABEL_CHARS || used + 1 >= size) return -1;
        out[used++] = c < 128 ? (char)tolower(c) : (char)c;
    }
    while (used > 0 && out[used - 1] == ' ') used--;
    out[used] = '\0';
    return used > 0 ? 0 : -1;
}
