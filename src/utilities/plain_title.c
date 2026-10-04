#include "utilities/plain_title.h"

#include <string.h>

/* How many bytes the UTF-8 character starting with `lead` takes. */
static size_t char_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;                                              /* a stray byte: dropped alone */
}

static void put(char *out, size_t size, size_t *used, const char *text) {
    for (; *text && *used + 1 < size; text++) {
        if (*text == ' ' && (*used == 0 || out[*used - 1] == ' ')) continue;   /* no leading or doubled space */
        out[(*used)++] = *text;
    }
}

void plain_title(const char *title, char *out, size_t size) {
    if (!out || size == 0) return;
    size_t used = 0;
    for (const char *p = title ? title : ""; *p; ) {
        unsigned char c = (unsigned char)*p;
        size_t n = char_length(c);
        if (strnlen(p, n) < n) break;                      /* cut short */
        if (c < 0x80) {
            char one[2] = { c >= 0x20 && c != 0x7F ? (char)c : ' ', '\0' };
            put(out, size, &used, one);
        } else if (n == 2 && c == 0xC2 && (unsigned char)p[1] == 0xB7) {
            put(out, size, &used, "-");                    /* · */
        } else if (n == 3 && c == 0xE2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0xA6) {
            put(out, size, &used, "...");                  /* … */
        }
        p += n;
    }
    while (used > 0 && out[used - 1] == ' ') used--;
    out[used] = '\0';
}
