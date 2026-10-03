/* The accounts dialog: moving between accounts, what each key asks its owner
 * for, typing a label for a new account or a new label for one, and what a
 * key does when no account is highlighted. */
#include "clients/tui/accounts_dialog.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static AccountsDialogRequest key(AccountsDialog *d, int ch) { return accounts_dialog_key(d, 0, ch); }
static AccountsDialogRequest code(AccountsDialog *d, int ch) { return accounts_dialog_key(d, 1, ch); }

static void type(AccountsDialog *d, const char *text) {
    for (; *text; text++) key(d, *text);
}

/* What a render would have left: the accounts shown. */
static void show(AccountsDialog *d, int count) {
    d->count = count;
    for (int i = 0; i < count; i++) d->ids[i] = i + 1;
    if (d->selected >= count) d->selected = count ? count - 1 : 0;
}

int main(void) {
    AccountsDialog d;
    accounts_dialog_open(&d);
    show(&d, 2);
    CHECK(d.open && accounts_dialog_selected(&d) == 1, "it opens on the first account");
    CHECK(code(&d, KEY_DOWN) == ACCOUNTS_REQUEST_REDRAW && accounts_dialog_selected(&d) == 2, "Down moves to the next");
    CHECK(code(&d, KEY_DOWN) == ACCOUNTS_REQUEST_NONE && accounts_dialog_selected(&d) == 2, "and stops at the last");
    CHECK(code(&d, KEY_UP) == ACCOUNTS_REQUEST_REDRAW && accounts_dialog_selected(&d) == 1, "Up moves back");
    accounts_dialog_select(&d, 2);
    CHECK(accounts_dialog_selected(&d) == 2, "an account can be highlighted by its id");

    CHECK(key(&d, '\n') == ACCOUNTS_REQUEST_VIEW, "Enter brings the highlighted account into view");
    CHECK(key(&d, 'p') == ACCOUNTS_REQUEST_PRIMARY, "p makes it primary");
    CHECK(key(&d, 'g') == ACCOUNTS_REQUEST_ACCESS, "g steps what agents may do with it");
    CHECK(key(&d, 'l') == ACCOUNTS_REQUEST_LOGOUT, "l logs it out");
    CHECK(key(&d, 'x') == ACCOUNTS_REQUEST_REMOVE && code(&d, KEY_DC) == ACCOUNTS_REQUEST_REMOVE, "x and Delete remove it");
    CHECK(key(&d, 's') == ACCOUNTS_REQUEST_SENDING, "s shows the contacts with a sending number of their own");

    /* A new account */
    CHECK(key(&d, 'a') == ACCOUNTS_REQUEST_REDRAW && d.typing, "a opens a line for the new account's label");
    type(&d, "Work");
    char *label = accounts_dialog_label(&d);
    CHECK(label && strcmp(label, "Work") == 0, "what is typed is the label");
    free(label);
    CHECK(key(&d, 'x') == ACCOUNTS_REQUEST_REDRAW && d.typing && d.open, "while typing, a letter is a letter and removes nothing");
    CHECK(key(&d, '\n') == ACCOUNTS_REQUEST_ADD, "Enter asks for the account to be added");
    accounts_dialog_error(&d, "Another account already has that label.");
    CHECK(d.typing && d.error[0], "a refused label keeps the line open with the reason");
    CHECK(code(&d, KEY_BACKSPACE) == ACCOUNTS_REQUEST_REDRAW && !d.error[0], "editing clears the reason");
    accounts_dialog_done(&d);
    CHECK(!d.typing && !d.error[0], "once it worked the list is back");

    /* A new label */
    CHECK(key(&d, 'r') == ACCOUNTS_REQUEST_REDRAW && d.typing, "r opens a line for a new label");
    type(&d, "AI");
    CHECK(key(&d, '\n') == ACCOUNTS_REQUEST_RENAME, "Enter asks for the highlighted account to be renamed");
    CHECK(key(&d, 27) == ACCOUNTS_REQUEST_REDRAW && !d.typing && d.open, "Esc leaves the line and keeps the dialog");
    CHECK(key(&d, 27) == ACCOUNTS_REQUEST_CLOSED && !d.open, "Esc again closes it");

    /* Nothing highlighted */
    accounts_dialog_open(&d);
    show(&d, 0);
    CHECK(accounts_dialog_selected(&d) == ACCOUNT_ID_NONE, "with no accounts shown nothing is highlighted");
    CHECK(key(&d, '\n') == ACCOUNTS_REQUEST_NONE && key(&d, 'p') == ACCOUNTS_REQUEST_NONE && key(&d, 'x') == ACCOUNTS_REQUEST_NONE &&
          key(&d, 'l') == ACCOUNTS_REQUEST_NONE && key(&d, 'g') == ACCOUNTS_REQUEST_NONE && key(&d, 'r') == ACCOUNTS_REQUEST_NONE,
          "then the keys that act on an account do nothing");
    CHECK(key(&d, 'a') == ACCOUNTS_REQUEST_REDRAW && d.typing, "but an account can still be added");

    if (failures == 0) printf("ok: the accounts dialog moves between accounts, asks for each change, and takes a label for a new account or a new name\n");
    return failures != 0;
}
