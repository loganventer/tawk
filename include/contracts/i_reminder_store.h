#ifndef APP_CONTRACTS_I_REMINDER_STORE_H
#define APP_CONTRACTS_I_REMINDER_STORE_H

#include "core/chat_reminder.h"

/* The chats you put aside, across your accounts. One reminder a chat. */
typedef struct IReminderStore {
    void *ctx;
    int  (*set)(struct IReminderStore *self, const ChatReminder *reminder);
    int  (*clear)(struct IReminderStore *self, const char *jid);
    int  (*list)(struct IReminderStore *self, ChatReminder *out, int max);
    void (*destroy)(struct IReminderStore *self);
} IReminderStore;

#endif
