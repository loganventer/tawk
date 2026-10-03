#ifndef APP_ENGINES_JID_LIST_H
#define APP_ENGINES_JID_LIST_H

#include <stddef.h>

/* A list of chats kept as text: JIDs separated by commas, or * for every chat. */

/* 1 when `jid` is named in `list`. A * names every chat. */
int jid_list_contains(const char *list, const char *jid);
/* Writes `list` with `jid` added (on) or taken out to `out`. A * is kept as
 * it is found. Returns 0, or -1 when the result does not fit. */
int jid_list_set(const char *list, const char *jid, int on, char *out, size_t size);

#endif
