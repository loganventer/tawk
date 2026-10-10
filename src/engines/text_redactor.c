#include "engines/text_redactor.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CARD_MARK "[card number]"
#define CODE_MARK "[code]"

static int word_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* Whether the message speaks of a code at all: without that, a short number is just a number. */
static int speaks_of_code(const char *text) {
    static const char *const WORDS[] = { "code", "otp", "pin", "password", "passcode", "verif", "kode", "wagwoord",
                                         "2fa", "one-time", "one time", "eenmalige", NULL };
    for (int i = 0; WORDS[i]; i++) {
        size_t n = strlen(WORDS[i]);
        for (const char *p = text; *p; p++) if (strncasecmp(p, WORDS[i], n) == 0) return 1;
    }
    return 0;
}

static int luhn(const char *digits, int count) {
    int sum = 0;
    for (int i = 0; i < count; i++) {
        int d = digits[count - 1 - i] - '0';
        if (i % 2 == 1) { d *= 2; if (d > 9) d -= 9; }
        sum += d;
    }
    return count > 0 && sum % 10 == 0;
}

/* A run of digits from `p`, in groups parted by one space or dash each. Returns its length in
 * bytes and writes the digits themselves, how many, and how many groups there were. */
static size_t digit_run(const char *p, char *digits, int max, int *count, int *groups) {
    size_t len = 0;
    *count = 0;
    *groups = 0;
    while (isdigit((unsigned char)p[len])) {
        (*groups)++;
        while (isdigit((unsigned char)p[len])) {
            if (*count < max) digits[*count] = p[len];
            (*count)++;
            len++;
        }
        if ((p[len] == ' ' || p[len] == '-') && isdigit((unsigned char)p[len + 1])) len++;
    }
    return len;
}

/* Where the first group of the run ends, for a number that is a code by itself. */
static size_t first_group(const char *p) {
    size_t n = 0;
    while (isdigit((unsigned char)p[n])) n++;
    return n;
}

char *text_redactor_redact(const char *text) {
    if (!text || !text[0]) return NULL;
    size_t size = strlen(text) * 2 + sizeof(CARD_MARK) + 1;
    char *out = malloc(size);
    if (!out) return NULL;
    int coded = speaks_of_code(text), changed = 0;
    size_t used = 0;
    const char *p = text;
    while (*p) {
        int starts = isdigit((unsigned char)*p) && (p == text || !word_char(p[-1]));
        if (!starts) { out[used++] = *p++; continue; }
        char digits[24];
        int count = 0, groups = 0;
        size_t len = digit_run(p, digits, (int)sizeof(digits), &count, &groups);
        const char *mark = NULL;
        size_t taken = len;
        if (!word_char(p[len]) && count >= 13 && count <= 19 && luhn(digits, count)) {
            mark = CARD_MARK;
        } else if (coded) {
            size_t one = first_group(p);
            int two_threes = groups == 2 && count == 6 && one == 3 && !word_char(p[len]);
            if (two_threes) mark = CODE_MARK;
            else if (one >= 4 && one <= 8 && !word_char(p[one]) && p[one] != '-' && !(p[one] == '.' && isdigit((unsigned char)p[one + 1]))
                     && !(p[one] == ':' && isdigit((unsigned char)p[one + 1])) && !(p[one] == '/' && isdigit((unsigned char)p[one + 1]))) {
                mark = CODE_MARK;
                taken = one;
            }
        }
        if (mark) {
            size_t n = strlen(mark);
            memcpy(out + used, mark, n);
            used += n;
            p += taken;
            changed = 1;
        } else {
            /* Not one of ours: the whole number is copied, so its later groups are not looked at again by themselves. */
            size_t group = first_group(p);
            memcpy(out + used, p, group);
            used += group;
            p += group;
        }
    }
    out[used] = '\0';
    if (!changed) { free(out); return NULL; }
    return out;
}
