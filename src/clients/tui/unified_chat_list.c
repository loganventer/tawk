#include "clients/tui/unified_chat_list.h"
#include "engines/chat_merge_policy.h"
#include "engines/reply_account_policy.h"
#include "core/account.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

void unified_chat_list_init(UnifiedChatList *list) {
    memset(list, 0, sizeof(*list));
}

void unified_chat_list_free(UnifiedChatList *list) {
    free(list->rows);
    memset(list, 0, sizeof(*list));
}

static int reserve(UnifiedChatList *list, int rows) {
    if (rows <= list->capacity) return 0;
    int cap = list->capacity ? list->capacity : 256;
    while (cap < rows) cap *= 2;
    Chat *grown = realloc(list->rows, (size_t)cap * sizeof(Chat));
    if (!grown) return -1;
    list->rows = grown;
    list->capacity = cap;
    return 0;
}

/* By contact, then by account bit, so the chats of one contact sit together. */
static int by_jid(const void *a, const void *b) {
    const Chat *x = a, *y = b;
    int c = strcmp(x->jid, y->jid);
    if (c != 0) return c;
    return x->accounts < y->accounts ? -1 : x->accounts > y->accounts;
}

/* Folds rows[from..to), the chats of one contact, into rows[from]. */
/* Online on any account is online; otherwise the most recent time they were seen. */
static void merge_presence(Chat *merged, const Chat *c) {
    if (c->presence == PRESENCE_ONLINE) merged->presence = PRESENCE_ONLINE;
    else if (c->presence == PRESENCE_OFFLINE && merged->presence == PRESENCE_UNKNOWN) merged->presence = PRESENCE_OFFLINE;
    if (c->last_seen > merged->last_seen) merged->last_seen = c->last_seen;
}

static void merge_run(Chat *rows, int from, int to, const ChatSource *sources, int source_count, const UnifiedChatRules *rules,
                      AccountId for_contact) {
    ReplyAccountCandidate candidates[ACCOUNT_MAX];
    int n = 0;
    for (int i = 0; i < source_count && n < ACCOUNT_MAX; i++) {
        candidates[n].id = sources[i].account;
        candidates[n].has_chat = 0;
        candidates[n].newest_ts = 0;
        n++;
    }
    Chat merged = rows[from];
    for (int i = from; i < to; i++) {
        const Chat *c = &rows[i];
        for (int k = 0; k < n; k++) {
            if (candidates[k].id != c->account) continue;
            candidates[k].has_chat = 1;
            candidates[k].newest_ts = c->last_ts;
        }
        if (i == from) continue;
        merged.accounts |= c->accounts;
        if (c->last_ts > merged.last_ts) {                    /* the newest message speaks for the chat */
            merged.last_ts = c->last_ts;
            str_copy(merged.preview, sizeof(merged.preview), c->preview);
        }
        if (!merged.name[0]) str_copy(merged.name, sizeof(merged.name), c->name);
        merged.unread += c->unread > 0 ? c->unread : 0;
        merged.unread_mention |= c->unread_mention;
        merged.is_pinned |= c->is_pinned;
        merged.has_draft |= c->has_draft;
        merged.soft_locked |= c->soft_locked;
        if (c->is_locked == 1) merged.is_locked = 1;          /* locked anywhere keeps it out of sight everywhere */
        merged.is_archived = merged.is_archived == 1 && c->is_archived == 1;   /* archived only when it is so on every account */
        merged.is_muted = merged.is_muted && c->is_muted;      /* muted only when no account would notify */
        if (!merged.typing[0]) str_copy(merged.typing, sizeof(merged.typing), c->typing);
        merge_presence(&merged, c);
    }
    AccountId send = reply_account_policy_choose(ACCOUNT_ID_NONE, for_contact, rules->primary, candidates, n);
    if (send != ACCOUNT_ID_NONE) merged.account = send;
    rows[from] = merged;
}

void unified_chat_list_build(UnifiedChatList *list, const ChatSource *sources, int source_count, const UnifiedChatRules *rules) {
    list->count = 0;
    int total = 0;
    for (int i = 0; i < source_count; i++) {
        if (rules->only == ACCOUNT_ID_NONE || rules->only == sources[i].account) total += sources[i].count;
    }
    if (total == 0 || reserve(list, total) != 0) return;
    int shown = 0;
    for (int i = 0; i < source_count; i++) {
        if (rules->only != ACCOUNT_ID_NONE && rules->only != sources[i].account) continue;
        shown++;
        for (int k = 0; k < sources[i].count; k++) {
            Chat *row = &list->rows[list->count++];
            *row = sources[i].chats[k];
            row->account = sources[i].account;
            row->accounts = 1u << i;
        }
    }
    if (shown > 1) {
        qsort(list->rows, (size_t)list->count, sizeof(Chat), by_jid);
        int out = 0;
        for (int from = 0; from < list->count;) {
            int to = from + 1;
            while (to < list->count && strcmp(list->rows[to].jid, list->rows[from].jid) == 0) to++;
            int merges = 0;
            if (to - from > 1) {
                ChatPrefs prefs;
                memset(&prefs, 0, sizeof(prefs));
                if (rules->prefs) rules->prefs(rules->ctx, list->rows[from].jid, &prefs);
                merges = chat_merge_policy_merges(rules->merge_setting, prefs.merge);
                if (merges) merge_run(list->rows, from, to, sources, source_count, rules, prefs.send_account);
            }
            if (merges) {
                list->rows[out++] = list->rows[from];
            } else {
                for (int i = from; i < to; i++) list->rows[out++] = list->rows[i];
            }
            from = to;
        }
        list->count = out;
    }
    qsort(list->rows, (size_t)list->count, sizeof(Chat), chat_compare);
}

const Chat *unified_chat_list_find(const UnifiedChatList *list, AccountId account, const char *jid) {
    if (!jid || !jid[0]) return NULL;
    const Chat *any = NULL;
    for (int i = 0; i < list->count; i++) {
        const Chat *row = &list->rows[i];
        if (strcmp(row->jid, jid) != 0) continue;
        if (account == ACCOUNT_ID_NONE || row->account == account) return row;
        if (!any) any = row;
    }
    /* A merged row acts through its sending account, yet holds the chat of every account in it. */
    return any && any->accounts && (any->accounts & (any->accounts - 1)) ? any : NULL;
}
