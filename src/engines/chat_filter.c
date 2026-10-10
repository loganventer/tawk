#include "engines/chat_filter.h"
#include "engines/label_name.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

int chat_filter_shows(ChatFilterKind kind, const Chat *c, const ChatFilterFacts *f) {
    switch (kind) {
        case CHAT_FILTER_UNREAD:   return c->unread > 0 || c->unread_mention;
        case CHAT_FILTER_GROUPS:   return c->is_group != 0;
        case CHAT_FILTER_DIRECT:   return !c->is_group;
        case CHAT_FILTER_AWAITING: return f->awaiting;
        case CHAT_FILTER_SNOOZED:  return f->snoozed;
        case CHAT_FILTER_LABEL:    return f->labelled;
        default:                   return !f->snoozed;
    }
}

static int word_is(const char *text, const char *word, const char **rest) {
    size_t n = strlen(word);
    if (strncasecmp(text, word, n) != 0 || (text[n] && !isspace((unsigned char)text[n]))) return 0;
    text += n;
    while (isspace((unsigned char)*text)) text++;
    *rest = text;
    return 1;
}

int chat_filter_parse(const char *text, ChatFilterKind *kind, char label[CHAT_LABEL_SIZE]) {
    static const struct { const char *word; ChatFilterKind kind; } WORDS[] = {
        { "off", CHAT_FILTER_NONE }, { "none", CHAT_FILTER_NONE }, { "all", CHAT_FILTER_NONE },
        { "unread", CHAT_FILTER_UNREAD }, { "groups", CHAT_FILTER_GROUPS }, { "direct", CHAT_FILTER_DIRECT },
        { "awaiting", CHAT_FILTER_AWAITING }, { "snoozed", CHAT_FILTER_SNOOZED },
    };
    label[0] = '\0';
    if (!text) text = "";
    while (isspace((unsigned char)*text)) text++;
    if (!*text) { *kind = CHAT_FILTER_NONE; return 0; }
    const char *rest = text;
    for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); i++) {
        if (word_is(text, WORDS[i].word, &rest) && !*rest) { *kind = WORDS[i].kind; return 0; }
    }
    if (word_is(text, "label", &rest) && label_name_clean(rest, label, CHAT_LABEL_SIZE) == 0) { *kind = CHAT_FILTER_LABEL; return 0; }
    return -1;
}

void chat_filter_title(ChatFilterKind kind, const char *label, char *out, size_t size) {
    switch (kind) {
        case CHAT_FILTER_UNREAD:   snprintf(out, size, "unread"); break;
        case CHAT_FILTER_GROUPS:   snprintf(out, size, "groups"); break;
        case CHAT_FILTER_DIRECT:   snprintf(out, size, "direct chats"); break;
        case CHAT_FILTER_AWAITING: snprintf(out, size, "awaiting a reply"); break;
        case CHAT_FILTER_SNOOZED:  snprintf(out, size, "snoozed"); break;
        case CHAT_FILTER_LABEL:    snprintf(out, size, "label %s", label ? label : ""); break;
        default:                   if (size) out[0] = '\0'; break;
    }
}
