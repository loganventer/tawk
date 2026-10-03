#ifndef APP_CLIENTS_TUI_ACCOUNTS_DIALOG_ROW_H
#define APP_CLIENTS_TUI_ACCOUNTS_DIALOG_ROW_H

#include "core/account.h"
#include "core/auth_state.h"

/* One account as the accounts dialog shows it. */
typedef struct AccountsDialogRow {
    Account   account;
    AuthState auth;              /* how its connection stands now */
    int       in_view;           /* the account whose conversation is on screen */
    int       unread;            /* chats with unread messages */
} AccountsDialogRow;

#endif
