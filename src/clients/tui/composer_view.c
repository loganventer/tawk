#include "core/icon_glyphs.h"
#include "clients/tui/composer_view.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "utilities/str_util.h"
#include "utilities/utf8_text.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <wctype.h>

#define MAX_LINES (COMPOSER_MAX_CHARS + 2)

/* ---- line layout -------------------------------------------------------- */

static int char_width(wchar_t c) {
    if (c == L'\n') return 0;
    int w = wcwidth(c);
    return w < 0 ? 1 : w;
}

/* Splits the text into visual lines `width` columns wide, breaking after the
 * last space when a word would overflow and mid-word only when a single word
 * is wider than the line. starts[i] is the first character of line i. */
static int layout_lines(const ComposerView *v, int width, int *starts) {
    if (width < 1) width = 1;
    int count = 1, col = 0, last_space = -1;
    starts[0] = 0;
    for (int i = 0; i < v->length; i++) {
        wchar_t c = v->text[i];
        if (c == L'\n') {
            if (count < MAX_LINES) starts[count++] = i + 1;
            col = 0;
            last_space = -1;
            continue;
        }
        int w = char_width(c);
        if (col + w > width && col > 0) {
            int brk = (last_space >= starts[count - 1]) ? last_space + 1 : i;
            if (count < MAX_LINES) starts[count++] = brk;
            col = 0;
            for (int k = brk; k < i; k++) col += char_width(v->text[k]);
            last_space = -1;
        }
        if (c == L' ') last_space = i;
        col += w;
    }
    return count;
}

/* End (exclusive) of line `line`, not counting a trailing newline. */
static int line_end(const ComposerView *v, const int *starts, int count, int line) {
    int end = line + 1 < count ? starts[line + 1] : v->length;
    if (end > starts[line] && v->text[end - 1] == L'\n') end--;
    return end;
}

static void caret_position(const ComposerView *v, const int *starts, int count, int *row, int *col) {
    int r = 0;
    while (r + 1 < count && starts[r + 1] <= v->cursor) r++;
    int c = 0;
    for (int i = starts[r]; i < v->cursor && i < v->length; i++) c += char_width(v->text[i]);
    *row = r;
    *col = c;
}

static int text_width(int composer_width, int has_buttons) {
    return composer_width - 4 - (has_buttons ? COMPOSER_BUTTONS_COLS : 0);
}

/* ---- editing ------------------------------------------------------------ */

void composer_view_init(ComposerView *v) { memset(v, 0, sizeof(*v)); v->last_width = 40; }

void composer_view_clear(ComposerView *v) {
    v->length = v->cursor = v->scroll_top = 0;
    v->text[0] = L'\0';
}

int composer_view_is_empty(const ComposerView *v) {
    for (int i = 0; i < v->length; i++) if (v->text[i] != L' ' && v->text[i] != L'\n') return 0;
    return 1;
}

void composer_view_set_text(ComposerView *v, const char *utf8) {
    composer_view_clear(v);
    if (!utf8 || !*utf8) return;
    size_t n = mbstowcs(v->text, utf8, COMPOSER_MAX_CHARS);
    if (n == (size_t)-1) { composer_view_clear(v); return; }
    v->length = (int)n;
    v->text[v->length] = L'\0';
    v->cursor = v->length;
}

void composer_view_insert(ComposerView *v, wchar_t ch) {
    if (v->length >= COMPOSER_MAX_CHARS) return;
    memmove(&v->text[v->cursor + 1], &v->text[v->cursor], (size_t)(v->length - v->cursor) * sizeof(wchar_t));
    v->text[v->cursor++] = ch;
    v->text[++v->length] = L'\0';
}

static void erase_at(ComposerView *v, int pos) {
    if (pos < 0 || pos >= v->length) return;
    memmove(&v->text[pos], &v->text[pos + 1], (size_t)(v->length - pos - 1) * sizeof(wchar_t));
    v->text[--v->length] = L'\0';
}

/* ↑/↓: same column on the previous or next wrapped line. */
static int move_vertical(ComposerView *v, int delta) {
    static int starts[MAX_LINES];
    int count = layout_lines(v, v->last_width, starts);
    int row, col;
    caret_position(v, starts, count, &row, &col);
    int target = row + delta;
    if (target < 0 || target >= count) return 0;
    int end = line_end(v, starts, count, target), used = 0, i = starts[target];
    while (i < end && used + char_width(v->text[i]) <= col) used += char_width(v->text[i++]);
    v->cursor = i;
    return 1;
}

int composer_view_key(ComposerView *v, int is_key_code, int ch) {
    if (is_key_code) {
        switch (ch) {
            case KEY_BACKSPACE: if (v->cursor > 0) erase_at(v, --v->cursor); return 1;
            case KEY_DC:        erase_at(v, v->cursor); return 1;
            case KEY_LEFT:      if (v->cursor > 0) v->cursor--; return 1;
            case KEY_RIGHT:     if (v->cursor < v->length) v->cursor++; return 1;
            case KEY_HOME:      v->cursor = 0; return 1;
            case KEY_END:       v->cursor = v->length; return 1;
            case KEY_UP:        return move_vertical(v, -1);
            case KEY_DOWN:      return move_vertical(v, 1);
            default:            return 0;
        }
    }
    switch (ch) {
        case 127: case 8: if (v->cursor > 0) erase_at(v, --v->cursor); return 1;
        case 1:  v->cursor = 0; return 1;                           /* Ctrl+A */
        case 5:  v->cursor = v->length; return 1;                   /* Ctrl+E */
        case 21: composer_view_clear(v); return 1;                  /* Ctrl+U */
        case 23:                                                    /* Ctrl+W: delete word */
            while (v->cursor > 0 && v->text[v->cursor - 1] == L' ') erase_at(v, --v->cursor);
            while (v->cursor > 0 && v->text[v->cursor - 1] != L' ') erase_at(v, --v->cursor);
            return 1;
        default: break;
    }
    if (ch >= 32 && ch != 127) {
        composer_view_insert(v, (wchar_t)ch);
        return 1;
    }
    return 0;
}

char *composer_view_text(const ComposerView *v) {
    return utf8_from_wide(v->text, (size_t)v->length);
}

int composer_view_rows_needed(const ComposerView *v, int composer_width, int has_buttons) {
    static int starts[MAX_LINES];
    int count = layout_lines(v, text_width(composer_width, has_buttons), starts);
    return count < 1 ? 1 : count > COMPOSER_MAX_ROWS ? COMPOSER_MAX_ROWS : count;
}

void composer_view_scroll(ComposerView *v, int delta) {
    v->scroll_top += delta;
    if (v->scroll_top < 0) v->scroll_top = 0;
}

int composer_view_hit_clear(const ComposerView *v, int y, int x)  { return ui_rect_contains(v->clear_button, y, x); }
int composer_view_hit_emoji(const ComposerView *v, int y, int x)  { return ui_rect_contains(v->emoji_button, y, x); }
int composer_view_hit_attach(const ComposerView *v, int y, int x) { return ui_rect_contains(v->attach_button, y, x); }
int composer_view_hit_send(const ComposerView *v, int y, int x)   { return ui_rect_contains(v->send_button, y, x); }

/* ---- drawing ------------------------------------------------------------ */

static void render_status(UiRect r, int has_chat, int recording_seconds, int enter_sends, const char *chip) {
    (void)enter_sends;
    int attr = tui_palette_attr(THEME_SLOT_DIM);
    char hint[320];
    if (recording_seconds >= 0) {
        snprintf(hint, sizeof(hint), " \xE2\x97\x8F REC %d:%02d   Enter send \xC2\xB7 Esc cancel",
                 recording_seconds / 60, recording_seconds % 60);
        attr = tui_palette_attr(THEME_SLOT_WARN) | ATTR_BOLD | ((recording_seconds % 2) ? 0 : ATTR_REVERSE);
    } else if (chip && *chip) {
        snprintf(hint, sizeof(hint), " %s", chip);
        attr = tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD;
    } else if (!has_chat) {
        snprintf(hint, sizeof(hint), " Open a chat to start typing");
    } else {
        hint[0] = '\0';          /* key hints live in the footer only */
    }
    tui_fill((UiRect){ r.y, r.x, 1, r.w }, tui_palette_attr(THEME_SLOT_BORDER));
    tui_text(r.y, r.x, r.w, hint, attr);
}

void composer_view_render(ComposerView *v, UiRect r, int focused, int has_chat,
                          int recording_seconds, int enter_sends, const char *chip) {
    int attr = tui_palette_attr(THEME_SLOT_COMPOSER);
    v->caret_y = v->caret_x = -1;
    v->clear_button = v->emoji_button = v->attach_button = v->send_button = (UiRect){ 0, 0, 0, 0 };
    tui_fill(r, attr);
    if (r.h < 2) return;
    render_status(r, has_chat, recording_seconds, enter_sends, chip);
    if (has_chat && recording_seconds < 0 && v->sending_as[0]) {       /* drawn apart from the chip, at the right */
        char label[104];
        snprintf(label, sizeof(label), "%s ", v->sending_as);
        tui_text_right(r.y, r.x + r.w, r.w / 2, label, tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD);
    }
    if (recording_seconds >= 0) return;

    int rows = r.h - 1;
    int buttons = has_chat && r.w > 30;
    int width = text_width(r.w, buttons);
    if (width < 4) return;
    v->last_width = width;

    static int starts[MAX_LINES];
    int count = layout_lines(v, width, starts);
    int caret_row, caret_col;
    caret_position(v, starts, count, &caret_row, &caret_col);

    /* Keep the caret visible without jumping when it already is. */
    int max_top = count - rows > 0 ? count - rows : 0;
    if (v->scroll_top > max_top) v->scroll_top = max_top;
    if (focused && caret_row < v->scroll_top) v->scroll_top = caret_row;
    if (focused && caret_row >= v->scroll_top + rows) v->scroll_top = caret_row - rows + 1;

    if (v->length == 0) {
        tui_text(r.y + 1, r.x + 2, width, !has_chat ? "" : (chip && *chip) ? "Add a caption (optional)" :
                 "Type your message here\xE2\x80\xA6", attr | ATTR_DIM);
    }
    for (int k = 0; k < rows && v->scroll_top + k < count; k++) {
        int line = v->scroll_top + k;
        int end = line_end(v, starts, count, line);
        attrset(attr);
        move(r.y + 1 + k, r.x + 2);
        for (int i = starts[line]; i < end; i++) {
            if (v->text[i] != 0xFE0F) addnwstr(&v->text[i], 1);    /* see tui_text_n */
        }
        attrset(A_NORMAL);
    }
    /* Scroll markers when lines are hidden above or below. */
    int marker_x = r.x + 2 + width;
    if (v->scroll_top > 0) tui_text(r.y + 1, marker_x, 1, "\xE2\x96\xB2", attr | ATTR_DIM);
    if (v->scroll_top + rows < count) tui_text(r.y + rows, marker_x, 1, "\xE2\x96\xBC", attr | ATTR_DIM);

    /* ✕, 😀, ➕ and ➤ at the bottom right of the input, like the app's send button. */
    if (buttons) {
        int by = r.y + rows;
        int bx = r.x + r.w - COMPOSER_BUTTONS_COLS;
        int can_send = !composer_view_is_empty(v) || (chip && chip[0]);
        if (!composer_view_is_empty(v)) {                 /* ✕ clears what is typed, after asking */
            tui_text(by, bx + 1, 1, "\xE2\x9C\x95", attr | ATTR_DIM);
            v->clear_button = (UiRect){ by, bx, 1, 3 };
        }
        tui_text(by, bx + 4, 2, "\xF0\x9F\x98\x80", attr);
        tui_text(by, bx + 7, 2, ICON_ATTACH, attr);
        tui_text(by, bx + 10, 2, ICON_SEND " ", can_send ? tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD : attr | ATTR_DIM);
        v->emoji_button = (UiRect){ by, bx + 3, 1, 3 };
        v->attach_button = (UiRect){ by, bx + 6, 1, 3 };
        v->send_button = (UiRect){ by, bx + 9, 1, 4 };
    }
    if (focused && caret_row >= v->scroll_top && caret_row < v->scroll_top + rows) {
        v->caret_y = r.y + 1 + (caret_row - v->scroll_top);
        v->caret_x = r.x + 2 + caret_col;
    }
}

int composer_view_convert_word(ComposerView *v, const char *(*convert)(const char *word)) {
    int end = v->cursor, start = end;
    while (start > 0 && !iswspace((wint_t)v->text[start - 1])) start--;
    if (end - start < 2 || end - start > 12) return 0;
    char word[64];
    mbstate_t st;
    memset(&st, 0, sizeof(st));
    size_t used = 0;
    for (int i = start; i < end; i++) {
        char mb[MB_LEN_MAX];
        size_t n = wcrtomb(mb, v->text[i], &st);
        if (n == (size_t)-1 || used + n >= sizeof(word)) return 0;
        memcpy(word + used, mb, n);
        used += n;
    }
    word[used] = '\0';
    const char *emoji = convert(word);
    return emoji ? composer_view_replace(v, start, end, emoji) : 0;
}

int composer_view_replace(ComposerView *v, int start, int end, const char *utf8) {
    if (start < 0 || end > v->length || start > end) return 0;
    wchar_t wide[64];
    size_t wn = mbstowcs(wide, utf8, sizeof(wide) / sizeof(wide[0]));
    if (wn == (size_t)-1 || wn >= sizeof(wide) / sizeof(wide[0])) return 0;
    if (v->length - (end - start) + (int)wn > COMPOSER_MAX_CHARS) return 0;
    memmove(&v->text[start + (int)wn], &v->text[end], sizeof(wchar_t) * (size_t)(v->length - end + 1));
    memcpy(&v->text[start], wide, sizeof(wchar_t) * wn);
    v->length += (int)wn - (end - start);
    v->cursor = start + (int)wn;
    return 1;
}

int composer_view_mention(const ComposerView *v, char *out, size_t size, int *start) {
    int end = v->cursor;
    int at = end - 1;
    while (at >= 0 && v->text[at] != L'@') {
        wchar_t c = v->text[at];
        if (iswspace((wint_t)c) || end - at > 30) return 0;
        at--;
    }
    if (at < 0 || (at > 0 && !iswspace((wint_t)v->text[at - 1]))) return 0;   /* "me@home" is not a mention */
    char *word = utf8_from_wide(&v->text[at + 1], (size_t)(end - at - 1));
    if (!word) return 0;
    str_copy(out, size, word);
    free(word);
    if (start) *start = at;
    return 1;
}

int composer_view_shortcode(const ComposerView *v, char *out, size_t size, int *start, int *closed) {
    int end = v->cursor;
    int shut = end > 0 && v->text[end - 1] == L')';
    int last = shut ? end - 2 : end - 1;            /* the word's last character */
    if (last < 1 || size < 2) return 0;
    int open = last;
    while (open >= 0 && v->text[open] != L'(') {
        wchar_t c = v->text[open];
        if (c > 127 || !(iswalnum((wint_t)c) || c == L' ') || last - open >= 30) return 0;
        open--;
    }
    int len = last - open;
    if (open < 0 || len < 2 || v->text[open + 1] == L' ') return 0;
    if (open > 0 && !iswspace((wint_t)v->text[open - 1])) return 0;   /* "f(x" is not a shortcode */
    size_t n = 0;
    for (int i = open + 1; i <= last && n + 1 < size; i++) out[n++] = (char)towlower((wint_t)v->text[i]);
    out[n] = '\0';
    if (start) *start = open;
    if (closed) *closed = shut;
    return 1;
}
