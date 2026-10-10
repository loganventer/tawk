#include "engines/summary_policy.h"

#include <string.h>

/* Characters, not bytes: a byte that continues a character is not counted. */
static int characters(const char *text) {
    int n = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) if ((*c & 0xC0) != 0x80) n++;
    return n;
}

int summary_policy_allows(const Chat *chat, const ChatPrefs *prefs) {
    if (!chat || chat->is_locked > 0 || chat->soft_locked) return 0;
    return prefs && prefs->tldr;
}

int summary_policy_wants(const Chat *chat, const ChatPrefs *prefs, const Message *message, int min_chars) {
    if (!summary_policy_allows(chat, prefs)) return 0;
    if (!message || message->type != MESSAGE_TYPE_TEXT || message->deleted || !message->text) return 0;
    int chars = characters(message->text);
    return chars > 0 && chars >= min_chars;
}

int summary_policy_shorter(const char *original, const char *summary) {
    if (!original || !summary || !summary[0]) return 0;
    return characters(summary) < characters(original);
}
