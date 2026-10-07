#ifndef APP_ENGINES_PRESENCE_TEXT_H
#define APP_ENGINES_PRESENCE_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "core/presence_state.h"

/* The line under a chat's name: "online", "last seen today at 14:32",
 * "last seen yesterday at 9:10", "last seen Monday at 18:05" within a week,
 * else "last seen 3 Oct". Empty when nothing is known, and when they are
 * offline and do not share when they were last here. */
void presence_text_format(PresenceState state, int64_t last_seen, int64_t now, int use_24h, char *out, size_t size);

#endif
