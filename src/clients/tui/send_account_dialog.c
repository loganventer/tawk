#include "clients/tui/send_account_dialog.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "utilities/str_util.h"

#include <ncurses.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 70

void send_account_dialog_open(SendAccountDialog *d) {
    memset(d, 0, sizeof(*d));
    d->open = 1;
}

const char *send_account_dialog_selected(const SendAccountDialog *d) {
    return d->selected >= 0 && d->selected < d->count ? d->jids[d->selected] : NULL;
}

static SendAccountRequest close_it(SendAccountDialog *d) {
    d->open = 0;
    return SEND_ACCOUNT_REQUEST_CLOSED;
}

static SendAccountRequest on_selected(const SendAccountDialog *d, SendAccountRequest request) {
    return send_account_dialog_selected(d) ? request : SEND_ACCOUNT_REQUEST_NONE;
}

SendAccountRequest send_account_dialog_key(SendAccountDialog *d, int is_key, int ch) {
    if (!is_key && (ch == 27 || ch == 'q')) return close_it(d);
    if (is_key && ch == KEY_UP && d->selected > 0) { d->selected--; return SEND_ACCOUNT_REQUEST_REDRAW; }
    if (is_key && ch == KEY_DOWN && d->selected < d->count - 1) { d->selected++; return SEND_ACCOUNT_REQUEST_REDRAW; }
    if ((!is_key && (ch == '\n' || ch == '\r' || ch == ' ')) || (is_key && ch == KEY_ENTER)) return on_selected(d, SEND_ACCOUNT_REQUEST_STEP);
    if ((is_key && ch == KEY_DC) || (!is_key && (ch == 'x' || ch == 'p'))) return on_selected(d, SEND_ACCOUNT_REQUEST_RESET);
    return SEND_ACCOUNT_REQUEST_NONE;
}

SendAccountRequest send_account_dialog_click(SendAccountDialog *d, int y, int x) {
    if (!ui_rect_contains(d->last_rect, y, x)) return close_it(d);
    if (ui_rect_contains(d->list_rect, y, x)) {
        int row = d->scroll + (y - d->list_rect.y);
        if (row >= 0 && row < d->count) {
            int again = row == d->selected;
            d->selected = row;
            return again ? SEND_ACCOUNT_REQUEST_STEP : SEND_ACCOUNT_REQUEST_REDRAW;
        }
    }
    return SEND_ACCOUNT_REQUEST_NONE;
}

void send_account_dialog_render(SendAccountDialog *d, UiRect a, const SendAccountRow *rows, int count, const char *primary) {
    d->count = count < SEND_ACCOUNT_ROWS ? count : SEND_ACCOUNT_ROWS;
    for (int i = 0; i < d->count; i++) str_copy(d->jids[i], sizeof(d->jids[0]), rows[i].jid);
    if (d->selected >= d->count) d->selected = d->count > 0 ? d->count - 1 : 0;

    int w = a.w < WIDTH ? a.w : WIDTH;
    int h = (d->count > 0 ? d->count : 1) + 7;
    if (h > a.h) h = a.h;
    UiRect box = { a.y + (a.h - h) / 2, a.x + (a.w - w) / 2, h, w };
    d->last_rect = box;
    int base = tui_palette_attr(THEME_SLOT_BASE), dim = tui_palette_attr(THEME_SLOT_DIM);
    tui_box(box, "Contacts with their own sending number", tui_palette_attr(THEME_SLOT_BORDER));
    tui_fill((UiRect){ box.y + 1, box.x + 1, box.h - 2, box.w - 2 }, base);

    int room = box.h - 6;
    if (room < 1) room = 1;
    if (d->selected < d->scroll) d->scroll = d->selected;
    if (d->selected >= d->scroll + room) d->scroll = d->selected - room + 1;
    d->list_rect = (UiRect){ box.y + 2, box.x + 1, room, box.w - 2 };
    if (d->count == 0) {
        tui_text_center(box.y + 2, box.x, box.w, "Nobody yet: everyone is sent to from the primary account", dim);
    }
    for (int k = 0; k < room && d->scroll + k < d->count; k++) {
        int i = d->scroll + k, y = d->list_rect.y + k;
        int attr = i == d->selected ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) | ATTR_BOLD : base;
        tui_fill((UiRect){ y, box.x + 1, 1, box.w - 2 }, attr);
        char from[ACCOUNT_LABEL_SIZE + 16];
        snprintf(from, sizeof(from), "from %s ", rows[i].account);
        int right = tui_text_right(y, box.x + box.w - 1, box.w / 2, from, attr);
        tui_text(y, box.x + 2, box.w - 4 - right, rows[i].name[0] ? rows[i].name : rows[i].jid, attr);
    }
    char note[ACCOUNT_LABEL_SIZE + 64];
    snprintf(note, sizeof(note), "Everyone else is sent to from the primary account, %s", primary && primary[0] ? primary : "main");
    tui_text(box.y + box.h - 3, box.x + 2, box.w - 4, note, dim);
    tui_text(box.y + box.h - 2, box.x + 2, box.w - 4, "Enter next account \xC2\xB7 x back to primary \xC2\xB7 Esc close", dim);
}
