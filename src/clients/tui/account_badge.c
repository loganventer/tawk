#include "clients/tui/account_badge.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Colours a theme already has, so the badges suit whichever theme is on. The
 * first three are the ones themes keep apart, so two or three accounts, the
 * usual number, are told apart by colour as well as by letter. */
static const ThemeSlot COLOURS[ACCOUNT_COLOURS] = {
    THEME_SLOT_ACCENT, THEME_SLOT_MEDIA, THEME_SLOT_WARN, THEME_SLOT_SENDER,
    THEME_SLOT_OK, THEME_SLOT_UNREAD, THEME_SLOT_BADGE, THEME_SLOT_BLINK,
};

void account_badge_make(AccountBadge *badge, const Account *account) {
    memset(badge, 0, sizeof(*badge));
    badge->account = account->id;
    const unsigned char *label = (const unsigned char *)account->label;
    size_t len = 1;                                        /* the bytes of the first character */
    if (label[0] >= 0xF0) len = 4;
    else if (label[0] >= 0xE0) len = 3;
    else if (label[0] >= 0xC0) len = 2;
    if (!label[0]) { badge->mark[0] = '?'; }
    else if (len == 1) { badge->mark[0] = (char)toupper(label[0]); }
    else { memcpy(badge->mark, label, len < sizeof(badge->mark) ? len : sizeof(badge->mark) - 1); }
    int colour = account->colour >= 0 ? account->colour % ACCOUNT_COLOURS : 0;
    badge->attr = tui_palette_attr(COLOURS[colour]) | ATTR_REVERSE | ATTR_BOLD;
}

static void text_of(const AccountBadge *badge, char *out, size_t size) {
    snprintf(out, size, " %s ", badge->mark);
}

int account_badge_draw_right(const AccountBadge *badge, int y, int right_x, int max_cols) {
    char text[16];
    text_of(badge, text, sizeof(text));
    return tui_text_right(y, right_x, max_cols, text, badge->attr);
}

int account_badge_draw(const AccountBadge *badge, int y, int x, int max_cols) {
    char text[16];
    text_of(badge, text, sizeof(text));
    return tui_text(y, x, max_cols, text, badge->attr);
}
