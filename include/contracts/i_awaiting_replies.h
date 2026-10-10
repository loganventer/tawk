#ifndef APP_CONTRACTS_I_AWAITING_REPLIES_H
#define APP_CONTRACTS_I_AWAITING_REPLIES_H

#include <stdint.h>

/* The one-to-one chats of an account whose newest message is yours and was
 * sent before `before` and after `after`: nobody has answered it. Read from
 * the messages already kept; nothing new is stored. */
typedef struct IAwaitingReplies {
    void *ctx;
    /* Writes up to `max` JIDs. Returns how many, or -1. */
    int  (*list)(struct IAwaitingReplies *self, int64_t after, int64_t before, char (*jids)[128], int max);
    void (*destroy)(struct IAwaitingReplies *self);
} IAwaitingReplies;

#endif
