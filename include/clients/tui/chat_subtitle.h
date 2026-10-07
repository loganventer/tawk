#ifndef APP_CLIENTS_TUI_CHAT_SUBTITLE_H
#define APP_CLIENTS_TUI_CHAT_SUBTITLE_H

#include <stddef.h>
#include <stdint.h>

#include "core/chat.h"
#include "core/settings.h"

/* The dim line under the name of the open chat. For one person it says
 * "online" or when they were last seen, when that is known and the setting
 * allows; otherwise it is `summary` (their about text, a group's member
 * count). A soft-locked chat shows nothing. */
void chat_subtitle_text(const Settings *settings, const Chat *chat, const char *summary, int64_t now, char *out, size_t size);

#endif
