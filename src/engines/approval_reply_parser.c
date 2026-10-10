#include "engines/approval_reply_parser.h"

#include <ctype.h>
#include <string.h>

#define THUMBS_UP   "\xF0\x9F\x91\x8D"
#define THUMBS_DOWN "\xF0\x9F\x91\x8E"
#define TICK        "\xE2\x9C\x85"
#define CROSS       "\xE2\x9D\x8C"

static int starts_with(const char *text, const char *emoji) { return strncmp(text, emoji, strlen(emoji)) == 0; }

ApprovalReply approval_reply_reaction(const char *emoji) {
    if (!emoji || !emoji[0]) return APPROVAL_REPLY_NONE;
    if (starts_with(emoji, THUMBS_UP) || starts_with(emoji, TICK)) return APPROVAL_REPLY_ALLOW;
    if (starts_with(emoji, THUMBS_DOWN) || starts_with(emoji, CROSS)) return APPROVAL_REPLY_DECLINE;
    return APPROVAL_REPLY_NONE;
}

static int one_of(const char *word, const char *const *list) {
    for (int i = 0; list[i]; i++) if (strcmp(word, list[i]) == 0) return 1;
    return 0;
}

ApprovalReply approval_reply_parse(const char *text) {
    static const char *const YES[] = { "y", "yes", "ok", "okay", "ja", "send", "approve", NULL };
    static const char *const NO[] = { "n", "no", "nee", "decline", "cancel", NULL };
    if (!text) return APPROVAL_REPLY_NONE;
    while (isspace((unsigned char)*text)) text++;
    if (!*text) return APPROVAL_REPLY_NONE;
    ApprovalReply by_emoji = approval_reply_reaction(text);
    char word[16];
    size_t n = 0;
    const char *p = text;
    while (*p && !isspace((unsigned char)*p) && n < sizeof(word) - 1) word[n++] = (char)tolower((unsigned char)*p++);
    word[n] = '\0';
    while (n > 0 && (word[n - 1] == '.' || word[n - 1] == '!')) word[--n] = '\0';
    while (isspace((unsigned char)*p)) p++;
    if (by_emoji != APPROVAL_REPLY_NONE) {
        /* A thumb and nothing after it but its skin tone or variation mark. */
        const char *rest = text + 4;
        while (*rest && ((unsigned char)*rest & 0x80)) rest++;
        while (isspace((unsigned char)*rest)) rest++;
        return *rest ? APPROVAL_REPLY_EDIT : by_emoji;
    }
    if (*p == '\0' && one_of(word, YES)) return APPROVAL_REPLY_ALLOW;
    if (*p == '\0' && one_of(word, NO)) return APPROVAL_REPLY_DECLINE;
    return APPROVAL_REPLY_EDIT;
}
