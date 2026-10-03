#ifndef APP_ENGINES_REPLY_ACCOUNT_CANDIDATE_H
#define APP_ENGINES_REPLY_ACCOUNT_CANDIDATE_H

#include <stdint.h>

#include "core/account_id.h"

/* One of your accounts, as far as sending to one contact goes. */
typedef struct ReplyAccountCandidate {
    AccountId id;
    int       has_chat;      /* this account has a chat with the contact */
    int64_t   newest_ts;     /* when that chat last had a message */
} ReplyAccountCandidate;

#endif
