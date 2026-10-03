#ifndef APP_CLIENTS_TUI_SEND_ACCOUNT_DIALOG_H
#define APP_CLIENTS_TUI_SEND_ACCOUNT_DIALOG_H

#include "clients/tui/send_account_request.h"
#include "clients/tui/send_account_row.h"
#include "clients/tui/ui_rect.h"

#define SEND_ACCOUNT_ROWS 64

/* Every contact that sends from an account of its own instead of the
 * primary one, to look them over in one place and change or reset each. */
typedef struct SendAccountDialog {
    int    open;
    int    selected;
    int    scroll;
    char   jids[SEND_ACCOUNT_ROWS][128];     /* the contacts shown, from the last render */
    int    count;
    UiRect last_rect;
    UiRect list_rect;
} SendAccountDialog;

void               send_account_dialog_open(SendAccountDialog *dialog);
SendAccountRequest send_account_dialog_key(SendAccountDialog *dialog, int is_key_code, int ch);
SendAccountRequest send_account_dialog_click(SendAccountDialog *dialog, int y, int x);
/* The highlighted contact's JID, or NULL when there is none. */
const char        *send_account_dialog_selected(const SendAccountDialog *dialog);
/* `primary` is the label of the primary account, named in the note below the list. */
void               send_account_dialog_render(SendAccountDialog *dialog, UiRect area, const SendAccountRow *rows, int count,
                                              const char *primary);

#endif
