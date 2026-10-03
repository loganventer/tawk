#include "engines/account_label_validator.h"
#include "utilities/str_util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *skip_spaces(const char *s) {
    while (*s == ' ') s++;
    return s;
}

/* The length without the spaces at the end. */
static size_t trimmed_len(const char *s) {
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') n--;
    return n;
}

int account_label_validate(const char *label, char *why, size_t why_size) {
    if (why && why_size) why[0] = '\0';
    const char *text = skip_spaces(label ? label : "");
    size_t len = trimmed_len(text);
    if (len == 0) {
        if (why) str_copy(why, why_size, "An account needs a label.");
        return -1;
    }
    int chars = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x20 || c == 0x7F || c == ',') {
            if (why) str_copy(why, why_size, "A label cannot hold a comma or a line break.");
            return -1;
        }
        if ((c & 0xC0) != 0x80) chars++;                /* characters, not bytes */
    }
    if (chars > ACCOUNT_LABEL_MAX_CHARS) {
        if (why) snprintf(why, why_size, "A label can be at most %d characters (this is %d).", ACCOUNT_LABEL_MAX_CHARS, chars);
        return -1;
    }
    return 0;
}

int account_label_same(const char *a, const char *b) {
    a = skip_spaces(a ? a : "");
    b = skip_spaces(b ? b : "");
    size_t la = trimmed_len(a), lb = trimmed_len(b);
    if (la != lb) return 0;
    for (size_t i = 0; i < la; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    }
    return 1;
}
