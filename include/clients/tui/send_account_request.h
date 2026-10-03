#ifndef APP_CLIENTS_TUI_SEND_ACCOUNT_REQUEST_H
#define APP_CLIENTS_TUI_SEND_ACCOUNT_REQUEST_H

/* What the list of sending accounts asks its owner to do. */
typedef enum SendAccountRequest {
    SEND_ACCOUNT_REQUEST_NONE = 0,
    SEND_ACCOUNT_REQUEST_REDRAW,
    SEND_ACCOUNT_REQUEST_CLOSED,
    SEND_ACCOUNT_REQUEST_STEP,       /* the highlighted contact sends from the next account */
    SEND_ACCOUNT_REQUEST_RESET       /* the highlighted contact goes back to the primary account */
} SendAccountRequest;

#endif
