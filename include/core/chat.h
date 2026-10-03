#ifndef APP_CORE_CHAT_H
#define APP_CORE_CHAT_H

#include <stdint.h>

#include "core/account_id.h"

typedef struct Chat {
    char    jid[128];
    char    name[128];
    char    preview[256];
    int64_t last_ts;
    int     unread;
    int     is_group;
    int     is_muted;       /* derived from muted_until when loaded */
    int     is_pinned;      /* local preference, never overwritten by sync */
    int     is_archived;    /* -1 unknown in sync events */
    int     is_locked;      /* -1 unknown in sync events */
    int64_t muted_until;    /* 0 not muted, -1 always, else epoch seconds */
    char    tone[256];      /* per-chat notification sound; "" default, "none" silent */
    char    theme[48];      /* per-chat conversation theme id; "" uses the app theme */
    int     has_draft;
    int     soft_locked;    /* conversation hidden behind a blur until shown again; local only */
    char    typing[64];     /* "typing…", "Jan is typing…", filled by the manager */
    int     unread_mention; /* an unread message mentions you */
    /* Filled only where several accounts' chats are listed together; a store leaves them 0. */
    AccountId account;      /* the account this row acts through: the only one that has the chat, or the one that sends */
    unsigned  accounts;     /* which running accounts have the chat, one bit each, in the order they are listed */
} Chat;

void chat_init(Chat *chat, const char *jid);
int  chat_jid_is_group(const char *jid);
/* Pinned first, then newest first. qsort-compatible. */
int  chat_compare(const void *a, const void *b);

#endif
