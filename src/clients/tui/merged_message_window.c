#include "clients/tui/merged_message_window.h"
#include "core/account.h"

#include <stdlib.h>
#include <string.h>

void merged_message_window_init(MergedMessageWindow *w) {
    memset(w, 0, sizeof(*w));
}

void merged_message_window_free(MergedMessageWindow *w) {
    free(w->items);
    free(w->owners);
    memset(w, 0, sizeof(*w));
}

static int reserve(MergedMessageWindow *w, int rows) {
    if (rows <= w->capacity) return 0;
    int cap = w->capacity ? w->capacity : 128;
    while (cap < rows) cap *= 2;
    Message *items = realloc(w->items, (size_t)cap * sizeof(Message));
    if (!items) return -1;
    w->items = items;
    AccountId *owners = realloc(w->owners, (size_t)cap * sizeof(AccountId));
    if (!owners) return -1;
    w->owners = owners;
    w->capacity = cap;
    return 0;
}

/* Where the same message already sits among those added, looking back over
 * the ones with its timestamp; -1 when it is new. */
static int already_there(const MergedMessageWindow *w, const Message *m) {
    for (int i = w->count - 1; i >= 0 && w->items[i].timestamp == m->timestamp; i--) {
        if (strcmp(w->items[i].id, m->id) == 0) return i;
    }
    return -1;
}

void merged_message_window_build(MergedMessageWindow *w, const MessageSource *sources, int source_count, AccountId prefer) {
    w->count = 0;
    int total = 0;
    if (source_count > ACCOUNT_MAX) source_count = ACCOUNT_MAX;
    for (int i = 0; i < source_count; i++) total += sources[i].count;
    if (total == 0 || reserve(w, total) != 0) return;
    int next[ACCOUNT_MAX] = { 0 };
    for (;;) {
        /* The oldest message not yet taken; the earlier source wins a tie. */
        int from = -1;
        for (int i = 0; i < source_count; i++) {
            if (next[i] >= sources[i].count) continue;
            if (from < 0 || sources[i].messages[next[i]].timestamp < sources[from].messages[next[from]].timestamp) from = i;
        }
        if (from < 0) break;
        const Message *m = &sources[from].messages[next[from]++];
        int have = already_there(w, m);
        if (have >= 0) {
            if (sources[from].account == prefer) {           /* the copy of the account in view speaks for it */
                w->items[have] = *m;
                w->owners[have] = prefer;
            }
            continue;
        }
        w->items[w->count] = *m;
        w->owners[w->count] = sources[from].account;
        w->count++;
    }
}
