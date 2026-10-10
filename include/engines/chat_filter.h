#ifndef APP_ENGINES_CHAT_FILTER_H
#define APP_ENGINES_CHAT_FILTER_H

#include <stddef.h>

#include "core/chat.h"
#include "core/chat_filter_facts.h"
#include "core/chat_filter_kind.h"
#include "core/chat_label.h"

/* Whether the list, narrowed to `kind`, shows this chat. With no filter a
 * snoozed chat is left out: that is what putting it aside means. */
int chat_filter_shows(ChatFilterKind kind, const Chat *chat, const ChatFilterFacts *facts);
/* Reads what follows /filter: "unread", "groups", "direct", "awaiting",
 * "snoozed", "label NAME", or "off", "none", "all" and nothing for no
 * filter. Returns 0 and sets *kind (and the label), or -1. */
int chat_filter_parse(const char *text, ChatFilterKind *kind, char label[CHAT_LABEL_SIZE]);
/* The words shown above a narrowed list. */
void chat_filter_title(ChatFilterKind kind, const char *label, char *out, size_t size);

#endif
