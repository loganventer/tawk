#ifndef APP_CLIENTS_TUI_ACCOUNTS_DIALOG_REQUEST_H
#define APP_CLIENTS_TUI_ACCOUNTS_DIALOG_REQUEST_H

/* What the accounts dialog asks its owner to do. */
typedef enum AccountsDialogRequest {
    ACCOUNTS_REQUEST_NONE = 0,
    ACCOUNTS_REQUEST_REDRAW,
    ACCOUNTS_REQUEST_CLOSED,
    ACCOUNTS_REQUEST_VIEW,         /* bring the highlighted account into view */
    ACCOUNTS_REQUEST_ADD,          /* add an account with the label typed */
    ACCOUNTS_REQUEST_RENAME,       /* give the highlighted account the label typed */
    ACCOUNTS_REQUEST_PRIMARY,      /* make it the primary account */
    ACCOUNTS_REQUEST_ACCESS,       /* step what agents may do with it */
    ACCOUNTS_REQUEST_LOGOUT,       /* unlink it from WhatsApp */
    ACCOUNTS_REQUEST_REMOVE,       /* take it and everything kept for it away */
    ACCOUNTS_REQUEST_SENDING       /* look at the contacts with a sending account of their own */
} AccountsDialogRequest;

#endif
