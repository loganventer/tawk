#ifndef APP_CLIENTS_TUI_ACCOUNTS_DIALOG_H
#define APP_CLIENTS_TUI_ACCOUNTS_DIALOG_H

#include "clients/tui/accounts_dialog_request.h"
#include "clients/tui/accounts_dialog_row.h"
#include "clients/tui/text_caret.h"
#include "clients/tui/text_field.h"
#include "clients/tui/ui_rect.h"

/* Your accounts: which is primary, how each is connected and what agents may
 * do with it, with the keys to add, name, link and remove them. Adding and
 * renaming open a line to type the label in; the owner checks it. */
typedef struct AccountsDialog {
    int       open;
    int       selected;
    AccountId ids[ACCOUNT_MAX];          /* the accounts shown, from the last render */
    int       count;
    int       typing;                    /* 0 no, 1 a label for a new account, 2 a new label for the highlighted one */
    TextField label;
    char      error[160];
    TextCaret caret;
    UiRect    last_rect;
    UiRect    list_rect;
} AccountsDialog;

void                  accounts_dialog_open(AccountsDialog *dialog);
AccountsDialogRequest accounts_dialog_key(AccountsDialog *dialog, int is_key_code, int ch);
AccountsDialogRequest accounts_dialog_click(AccountsDialog *dialog, int y, int x);
void                  accounts_dialog_paste(AccountsDialog *dialog, const char *utf8);
/* The highlighted account, or ACCOUNT_ID_NONE when there is none. */
AccountId             accounts_dialog_selected(const AccountsDialog *dialog);
void                  accounts_dialog_select(AccountsDialog *dialog, AccountId account);
/* The label typed, as UTF-8; the caller frees it. */
char                 *accounts_dialog_label(const AccountsDialog *dialog);
/* After ADD or RENAME: it worked (back to the list), or why not. */
void                  accounts_dialog_done(AccountsDialog *dialog);
void                  accounts_dialog_error(AccountsDialog *dialog, const char *why);
void                  accounts_dialog_render(AccountsDialog *dialog, UiRect area, const AccountsDialogRow *rows, int count);

#endif
