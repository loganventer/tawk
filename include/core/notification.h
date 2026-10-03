#ifndef APP_CORE_NOTIFICATION_H
#define APP_CORE_NOTIFICATION_H

#include "core/account_id.h"
#include "core/message_type.h"

typedef struct Notification {
    char        chat_jid[128];
    char        title[128];
    char        body[256];
    MessageType type;
    int         is_group;
    char        tone[256];    /* chat-specific sound; "" default, "none" silent */
    AccountId   account;      /* which of your accounts the message reached */
    char        account_label[64];
} Notification;

#endif
