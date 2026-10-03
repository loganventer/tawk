#ifndef APP_CLIENTS_TUI_SEND_ACCOUNT_ROW_H
#define APP_CLIENTS_TUI_SEND_ACCOUNT_ROW_H

#include "core/account.h"

/* One contact with a sending account of their own, as the list shows it. */
typedef struct SendAccountRow {
    char jid[128];
    char name[128];
    char account[ACCOUNT_LABEL_SIZE];   /* the label of the account that sends to them */
} SendAccountRow;

#endif
