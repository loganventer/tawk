#include "clients/tui/chat_subtitle.h"

#include "engines/presence_text.h"
#include "utilities/str_util.h"

void chat_subtitle_text(const Settings *settings, const Chat *chat, const char *summary, int64_t now, char *out, size_t size) {
    if (!out || size == 0) return;
    out[0] = '\0';
    if (!chat || chat->soft_locked) return;
    if (settings->show_online && !chat->is_group) {
        presence_text_format(chat->presence, chat->last_seen, now, settings->use_24h_clock, out, size);
    }
    if (!out[0]) str_copy(out, size, summary ? summary : "");
}
