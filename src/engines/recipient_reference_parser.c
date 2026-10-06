#include "engines/recipient_reference_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* What the phone number standard allows, country code included. */
#define MIN_DIGITS 8
#define MAX_DIGITS 15

static const char PERSON[] = "@s.whatsapp.net";

int recipient_reference_is_person(const char *jid) {
    const char *at = jid ? strchr(jid, '@') : NULL;
    if (!at || at == jid || strcmp(at, PERSON) != 0) return 0;
    for (const char *p = jid; p < at; p++) if (!isdigit((unsigned char)*p)) return 0;
    return 1;
}

/* The digits of a number written with spaces, dashes, brackets or dots; -1 when anything else is in it. */
static int digits_of(const char *ref, char *out, size_t size) {
    size_t k = 0;
    const char *p = ref;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '+') p++;
    for (; *p; p++) {
        if (isdigit((unsigned char)*p)) {
            if (k + 1 >= size) return -1;
            out[k++] = *p;
        } else if (!isspace((unsigned char)*p) && *p != '-' && *p != '(' && *p != ')' && *p != '.') {
            return -1;
        }
    }
    out[k] = '\0';
    return (int)k;
}

int recipient_reference_parse(const char *ref, char *jid, unsigned long size) {
    if (!ref || !*ref || !jid || size == 0) return -1;
    if (strchr(ref, '@')) {
        if (!recipient_reference_is_person(ref) || strlen(ref) >= size) return -1;
        size_t digits = (size_t)(strchr(ref, '@') - ref);
        if (digits < MIN_DIGITS || digits > MAX_DIGITS) return -1;
        snprintf(jid, size, "%s", ref);
        return 0;
    }
    char digits[32];
    int n = digits_of(ref, digits, sizeof(digits));
    if (n < 0) return -1;
    const char *number = digits;
    int plus = 0;
    for (const char *p = ref; *p && !isdigit((unsigned char)*p); p++) if (*p == '+') plus = 1;
    if (!plus && strncmp(number, "00", 2) == 0) { number += 2; n -= 2; }   /* 00 is how a country code is dialled */
    else if (!plus && number[0] == '0') return -1;                         /* local: no country in it */
    if (n < MIN_DIGITS || n > MAX_DIGITS || number[0] == '0') return -1;
    if ((size_t)n + sizeof(PERSON) > size) return -1;
    snprintf(jid, size, "%s%s", number, PERSON);
    return 0;
}
