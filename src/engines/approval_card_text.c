#include "engines/approval_card_text.h"

#include <stdio.h>
#include <string.h>

#define WORDS_MAX 3000

void approval_card_text(const ApprovalCard *c, char *out, size_t size) {
    size_t used = 0;
    const char *client = c->client && c->client[0] ? c->client : "An agent";
    used += (size_t)snprintf(out + used, size - used, "tawk: %s%s wants to %s", c->changed ? "changed. " : "", client,
                             c->action && c->action[0] ? c->action : "do something");
    if (used < size && c->chat_name && c->chat_name[0]) used += (size_t)snprintf(out + used, size - used, " in %s", c->chat_name);
    if (used < size && c->account_label && c->account_label[0]) used += (size_t)snprintf(out + used, size - used, " (from %s)", c->account_label);
    if (used < size && c->text && c->text[0]) {
        size_t n = strlen(c->text);
        int cut = n > WORDS_MAX;
        if (cut) { n = WORDS_MAX; while (n > 0 && ((unsigned char)c->text[n] & 0xC0) == 0x80) n--; }
        used += (size_t)snprintf(out + used, size - used, ":\n\n%.*s%s", (int)n, c->text, cut ? " [cut here; tawk shows all of it]" : "");
    }
    if (used >= size) return;
    used += (size_t)snprintf(out + used, size - used, "\n\nReply to this message with y to allow it or n to decline it, or react with a thumbs up or down.");
    if (used < size && c->editable) used += (size_t)snprintf(out + used, size - used, " Reply with other words to send those instead; they are read back first.");
    if (used < size && c->minutes_left > 0) snprintf(out + used, size - used, " It is declined by itself in %d minute%s.", c->minutes_left, c->minutes_left == 1 ? "" : "s");
}
