#ifndef APP_CONTRACTS_I_LABEL_STORE_H
#define APP_CONTRACTS_I_LABEL_STORE_H

#include "core/chat_label.h"

/* Your labels on chats, across your accounts. */
typedef struct ILabelStore {
    void *ctx;
    int  (*add)(struct ILabelStore *self, const char *jid, const char *label);
    int  (*remove)(struct ILabelStore *self, const char *jid, const char *label);
    /* Every label on every chat, by chat. Returns how many, or -1. */
    int  (*list)(struct ILabelStore *self, ChatLabel *out, int max);
    void (*destroy)(struct ILabelStore *self);
} ILabelStore;

#endif
