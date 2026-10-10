#include "engines/summary_policy.h"

#include <string.h>

int summary_policy_allows(const Chat *chat, const ChatPrefs *prefs) {
    if (!chat || chat->is_locked > 0 || chat->soft_locked) return 0;
    return prefs && prefs->tldr;
}

int summary_policy_wants(const Chat *chat, const ChatPrefs *prefs, const Message *message, int min_chars) {
    if (!summary_policy_allows(chat, prefs)) return 0;
    if (!message || message->type != MESSAGE_TYPE_TEXT || message->deleted || !message->text) return 0;
    /* Characters, not bytes: a byte that continues a character is not counted. */
    int chars = 0;
    for (const unsigned char *c = (const unsigned char *)message->text; *c; c++) if ((*c & 0xC0) != 0x80) chars++;
    return chars >= min_chars;
}
