#ifndef APP_CLIENTS_TUI_CONFIRM_PURPOSE_H
#define APP_CLIENTS_TUI_CONFIRM_PURPOSE_H

/* What a confirmation dialog is asking about, so the app knows what to do
 * when it is confirmed. */
typedef enum ConfirmPurpose {
    CONFIRM_NONE = 0,
    CONFIRM_DELETE_CHAT,
    CONFIRM_CLEAR_LOGS,
    CONFIRM_BLOCK,
    CONFIRM_CLEAR_CHAT,
    CONFIRM_REMOVE_PHOTO,        /* your own profile photo */
    CONFIRM_USE_WHATSMEOW,       /* switch backends to post a status */
    CONFIRM_ENABLE_AGENTS,       /* turn on the control socket */
    CONFIRM_CLEAR_INPUT,         /* throw away what is typed in the message input */
    CONFIRM_REMOVE_ACCOUNT,      /* take an account and everything kept for it away; the subject is its id */
    CONFIRM_LOGOUT_ACCOUNT       /* unlink an account from WhatsApp; the subject is its id */
} ConfirmPurpose;

#endif
