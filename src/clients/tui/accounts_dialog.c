#include "clients/tui/accounts_dialog.h"
#include "clients/tui/account_badge.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "engines/account_label_validator.h"
#include "utilities/str_util.h"

#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#define WIDTH       76
#define ROW_LINES   2
#define TYPING_NEW    1
#define TYPING_RENAME 2

void accounts_dialog_open(AccountsDialog *d) {
    memset(d, 0, sizeof(*d));
    text_field_init(&d->label, ACCOUNT_LABEL_MAX_CHARS);
    d->open = 1;
}

AccountId accounts_dialog_selected(const AccountsDialog *d) {
    return d->selected >= 0 && d->selected < d->count ? d->ids[d->selected] : ACCOUNT_ID_NONE;
}

void accounts_dialog_select(AccountsDialog *d, AccountId account) {
    for (int i = 0; i < d->count; i++) if (d->ids[i] == account) d->selected = i;
}

char *accounts_dialog_label(const AccountsDialog *d) { return text_field_text(&d->label); }

void accounts_dialog_done(AccountsDialog *d) {
    d->typing = 0;
    d->error[0] = '\0';
    d->caret.visible = 0;
}

void accounts_dialog_error(AccountsDialog *d, const char *why) { str_copy(d->error, sizeof(d->error), why ? why : ""); }

static AccountsDialogRequest close_it(AccountsDialog *d) {
    d->open = 0;
    d->caret.visible = 0;
    return ACCOUNTS_REQUEST_CLOSED;
}

static AccountsDialogRequest start_typing(AccountsDialog *d, int what, const char *text) {
    d->typing = what;
    d->error[0] = '\0';
    text_field_set(&d->label, text);
    return ACCOUNTS_REQUEST_REDRAW;
}

/* Something to act on only when an account is highlighted. */
static AccountsDialogRequest on_selected(const AccountsDialog *d, AccountsDialogRequest request) {
    return accounts_dialog_selected(d) != ACCOUNT_ID_NONE ? request : ACCOUNTS_REQUEST_NONE;
}

AccountsDialogRequest accounts_dialog_key(AccountsDialog *d, int is_key, int ch) {
    int enter = (!is_key && (ch == '\n' || ch == '\r')) || (is_key && ch == KEY_ENTER);
    if (d->typing) {
        if (!is_key && ch == 27) { accounts_dialog_done(d); return ACCOUNTS_REQUEST_REDRAW; }
        if (enter) return d->typing == TYPING_NEW ? ACCOUNTS_REQUEST_ADD : on_selected(d, ACCOUNTS_REQUEST_RENAME);
        if (text_field_key(&d->label, is_key, ch)) { d->error[0] = '\0'; return ACCOUNTS_REQUEST_REDRAW; }
        return ACCOUNTS_REQUEST_NONE;
    }
    if (!is_key && (ch == 27 || ch == 'q')) return close_it(d);
    if (is_key && ch == KEY_UP && d->selected > 0) { d->selected--; d->error[0] = '\0'; return ACCOUNTS_REQUEST_REDRAW; }
    if (is_key && ch == KEY_DOWN && d->selected < d->count - 1) { d->selected++; d->error[0] = '\0'; return ACCOUNTS_REQUEST_REDRAW; }
    if (enter) return on_selected(d, ACCOUNTS_REQUEST_VIEW);
    if (is_key) return (ch == KEY_DC) ? on_selected(d, ACCOUNTS_REQUEST_REMOVE) : ACCOUNTS_REQUEST_NONE;
    switch (ch) {
        case 'a': case '+': return start_typing(d, TYPING_NEW, "");
        case 'r':           return accounts_dialog_selected(d) != ACCOUNT_ID_NONE ? start_typing(d, TYPING_RENAME, "") : ACCOUNTS_REQUEST_NONE;
        case 'p':           return on_selected(d, ACCOUNTS_REQUEST_PRIMARY);
        case 'g':           return on_selected(d, ACCOUNTS_REQUEST_ACCESS);
        case 'l':           return on_selected(d, ACCOUNTS_REQUEST_LOGOUT);
        case 'x':           return on_selected(d, ACCOUNTS_REQUEST_REMOVE);
        case 's':           return ACCOUNTS_REQUEST_SENDING;
        default:            return ACCOUNTS_REQUEST_NONE;
    }
}

AccountsDialogRequest accounts_dialog_click(AccountsDialog *d, int y, int x) {
    if (!ui_rect_contains(d->last_rect, y, x)) return close_it(d);
    if (!d->typing && ui_rect_contains(d->list_rect, y, x)) {
        int row = (y - d->list_rect.y) / ROW_LINES;
        if (row >= 0 && row < d->count) {
            int again = row == d->selected;
            d->selected = row;
            return again ? ACCOUNTS_REQUEST_VIEW : ACCOUNTS_REQUEST_REDRAW;       /* a second click on a row opens it */
        }
    }
    return ACCOUNTS_REQUEST_NONE;
}

void accounts_dialog_paste(AccountsDialog *d, const char *utf8) {
    if (d->typing) text_field_paste(&d->label, utf8);
}

static const char *connection_text(AuthState auth) {
    switch (auth) {
        case AUTH_STATE_CONNECTED:   return "\xF0\x9F\x9F\xA2 connected";
        case AUTH_STATE_NEEDS_LOGIN: return "\xE2\x9A\xAA not linked";
        case AUTH_STATE_STARTING:    return "\xF0\x9F\x9F\xA1 starting";
        default:                     return "\xF0\x9F\x9F\xA1 connecting";
    }
}

void accounts_dialog_render(AccountsDialog *d, UiRect a, const AccountsDialogRow *rows, int count) {
    d->count = count < ACCOUNT_MAX ? count : ACCOUNT_MAX;
    for (int i = 0; i < d->count; i++) d->ids[i] = rows[i].account.id;
    if (d->selected >= d->count) d->selected = d->count > 0 ? d->count - 1 : 0;

    int w = a.w < WIDTH ? a.w : WIDTH;
    int h = d->count * ROW_LINES + 9;
    if (h > a.h) h = a.h;
    UiRect box = { a.y + (a.h - h) / 2, a.x + (a.w - w) / 2, h, w };
    d->last_rect = box;
    int base = tui_palette_attr(THEME_SLOT_BASE), dim = tui_palette_attr(THEME_SLOT_DIM);
    tui_box(box, "Accounts", tui_palette_attr(THEME_SLOT_BORDER));
    tui_fill((UiRect){ box.y + 1, box.x + 1, box.h - 2, box.w - 2 }, base);

    d->list_rect = (UiRect){ box.y + 2, box.x + 1, d->count * ROW_LINES, box.w - 2 };
    for (int i = 0; i < d->count; i++) {
        int y = d->list_rect.y + i * ROW_LINES;
        if (y + ROW_LINES > box.y + box.h - 5) { d->list_rect.h = i * ROW_LINES; break; }
        const AccountsDialogRow *row = &rows[i];
        int attr = i == d->selected ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) : base;
        tui_fill((UiRect){ y, box.x + 1, ROW_LINES, box.w - 2 }, attr);
        AccountBadge badge;
        account_badge_make(&badge, &row->account);
        int x = box.x + 2;
        x += account_badge_draw(&badge, y, x, 4) + 1;
        char title[160];
        snprintf(title, sizeof(title), "%s%s%s", row->account.label, row->account.is_primary ? "  \xE2\x98\x85 primary" : "",
                 row->in_view ? "  \xC2\xB7 in view" : "");
        tui_text(y, x, box.x + box.w - 2 - x, title, attr | ATTR_BOLD);
        char detail[256], unread[32] = "";
        if (row->unread > 0) snprintf(unread, sizeof(unread), "  \xC2\xB7 %d unread", row->unread);
        snprintf(detail, sizeof(detail), "%s  \xC2\xB7 %s  \xC2\xB7 agents: %s%s", connection_text(row->auth),
                 row->account.name[0] ? row->account.name : row->account.jid[0] ? row->account.jid : "no number yet",
                 account_agent_access_name(row->account.agent_access), unread);
        tui_text(y + 1, x, box.x + box.w - 2 - x, detail, attr | ATTR_DIM);
    }

    int foot = box.y + box.h - 4;
    d->caret.visible = 0;
    if (d->typing) {
        const char *prompt = d->typing == TYPING_NEW ? " Label for the new account: " : " New label: ";
        int px = box.x + 1 + tui_text(foot, box.x + 1, box.w - 2, prompt, base | ATTR_BOLD);
        text_field_render(&d->label, (UiRect){ foot, px, 1, box.x + box.w - 2 - px }, tui_palette_attr(THEME_SLOT_COMPOSER), 1, &d->caret);
        tui_text(foot + 2, box.x + 2, box.w - 4, "Enter save \xC2\xB7 Esc back", dim);
    } else {
        tui_text(foot, box.x + 2, box.w - 4, "Enter bring into view \xC2\xB7 a add \xC2\xB7 r rename \xC2\xB7 p make primary", dim);
        tui_text(foot + 1, box.x + 2, box.w - 4, "g agent access \xC2\xB7 l log out \xC2\xB7 x remove \xC2\xB7 s sending numbers \xC2\xB7 Esc close", dim);
    }
    if (d->error[0]) tui_text(foot + (d->typing ? 1 : 2), box.x + 2, box.w - 4, d->error, tui_palette_attr(THEME_SLOT_WARN));
}
