#include "engines/transcript_validator.h"
#include "core/transcript.h"
#include "utilities/str_util.h"

#include <ctype.h>
#include <string.h>

#define LANGUAGE_MAX 15

static int language_ok(const char *language) {
    size_t n = strlen(language);
    if (n > LANGUAGE_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)language[i];
        if (!isalnum(c) && c != '-' && c != '_') return 0;
    }
    return 1;
}

static int has_words(const char *text) {
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) if (*c > ' ') return 1;
    return 0;
}

const char *transcript_validator_refusal(const Message *message, const char *language, const char *text) {
    if (!message || message->type != MESSAGE_TYPE_AUDIO) return "Only a voice note or other audio has a transcript";
    if (message->deleted) return "That message was deleted";
    if (!text || !has_words(text)) return "\"text\" is required";
    if (strlen(text) > TRANSCRIPT_MAX_BYTES) return "The transcript is too long";
    if (language && !language_ok(language)) return "\"language\" is a short code such as en or af";
    return NULL;
}

void transcript_validator_clean(char *text) {
    if (!text) return;
    for (unsigned char *c = (unsigned char *)text; *c; c++) {
        if (*c == '\r' || *c == '\t') *c = ' ';
        else if (*c == 0x7F || (*c < ' ' && *c != '\n')) *c = ' ';
    }
    char *trimmed = str_trim(text);
    if (trimmed != text) memmove(text, trimmed, strlen(trimmed) + 1);
}

void transcript_validator_language(const char *language, char *out, size_t size) {
    str_copy(out, size, language ? language : "");
    for (char *c = out; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (strcmp(out, "auto") == 0) out[0] = '\0';
}
