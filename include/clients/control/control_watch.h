#ifndef APP_CLIENTS_CONTROL_CONTROL_WATCH_H
#define APP_CLIENTS_CONTROL_CONTROL_WATCH_H
#include "core/account_id.h"

/* A subscribed chat's unread count as last told to the client. */
typedef struct ControlWatch {
    char      jid[128];
    AccountId account;     /* the account the chat is in; the same person on two accounts is watched twice */
    int       unread;
} ControlWatch;

#endif
