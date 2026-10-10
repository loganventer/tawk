#include "engines/self_chat_rule.h"

#include <string.h>

/* How long the part that names the person is: up to the device or the server. */
static size_t user_length(const char *jid) {
    size_t n = 0;
    while (jid[n] && jid[n] != ':' && jid[n] != '@') n++;
    return n;
}

static const char *server_of(const char *jid) {
    const char *at = strchr(jid, '@');
    return at ? at + 1 : "";
}

int self_chat_rule_is(const char *own_jid, const char *chat_jid) {
    if (!own_jid || !chat_jid || !own_jid[0] || !chat_jid[0]) return 0;
    if (strcmp(server_of(chat_jid), "g.us") == 0 || strcmp(server_of(chat_jid), "broadcast") == 0) return 0;
    size_t own = user_length(own_jid), chat = user_length(chat_jid);
    if (own == 0 || own != chat || strncmp(own_jid, chat_jid, own) != 0) return 0;
    return strcmp(server_of(own_jid), server_of(chat_jid)) == 0;
}
