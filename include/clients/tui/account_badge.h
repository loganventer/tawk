#ifndef APP_CLIENTS_TUI_ACCOUNT_BADGE_H
#define APP_CLIENTS_TUI_ACCOUNT_BADGE_H

#include "core/account.h"

/* The small mark that says which account something belongs to: the first
 * letter of the account's label, in the account's colour. */
typedef struct AccountBadge {
    AccountId account;
    char      mark[8];       /* one character, UTF-8 */
    int       attr;
} AccountBadge;

void account_badge_make(AccountBadge *badge, const Account *account);
/* Draws " M " ending at right_x and returns the columns it took. */
int  account_badge_draw_right(const AccountBadge *badge, int y, int right_x, int max_cols);
/* Draws it at x and returns the columns it took. */
int  account_badge_draw(const AccountBadge *badge, int y, int x, int max_cols);

#endif
