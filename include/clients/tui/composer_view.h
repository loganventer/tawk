#ifndef APP_CLIENTS_TUI_COMPOSER_VIEW_H
#define APP_CLIENTS_TUI_COMPOSER_VIEW_H

#include <stddef.h>
#include <wchar.h>

#include "clients/tui/ui_rect.h"

#define COMPOSER_MAX_CHARS     4096
#define COMPOSER_MAX_ROWS      5     /* the input grows to this many lines, then scrolls */
#define COMPOSER_BUTTONS_COLS  13    /* " ✕ 😀 ➕ ➤ " at the right of the input; ✕ only while there is text */

/* The message input: a wide-character editor that word-wraps, grows up to
 * COMPOSER_MAX_ROWS lines and then scrolls, keeping the caret in view. */
typedef struct ComposerView {
    char      sending_as[96];   /* "as <account>": which of your numbers a message goes out from; "" shows nothing */
    wchar_t text[COMPOSER_MAX_CHARS + 1];
    int     length;
    int     cursor;
    int     scroll_top;      /* first visible wrapped line */
    int     last_width;      /* text columns used by the last render, for ↑/↓ */
    int     caret_y;         /* where the caret belongs after rendering, or -1 */
    int     caret_x;
    UiRect  clear_button;    /* ✕, drawn while there is something typed */
    UiRect  emoji_button;    /* last drawn positions of 😀, ➕ and ➤, for clicks */
    UiRect  attach_button;
    UiRect  send_button;
} ComposerView;

void  composer_view_init(ComposerView *view);
void  composer_view_clear(ComposerView *view);
int   composer_view_is_empty(const ComposerView *view);
/* Replaces the text (UTF-8) and puts the caret at the end. */
void  composer_view_set_text(ComposerView *view, const char *utf8);
/* Handles an editing key. Returns 1 when the key was consumed. ↑ and ↓
 * move between wrapped lines and return 0 at the first or last line. */
int   composer_view_key(ComposerView *view, int is_key_code, int ch);
/* Moves the cursor a word at a time: to the start of the word before it (-1)
 * or past the end of the word after it (+1). */
void  composer_view_move_word(ComposerView *view, int direction);
/* Moves the cursor to the very start (-1) or the very end (+1) of what is typed. */
void  composer_view_move_end(ComposerView *view, int direction);
void  composer_view_insert(ComposerView *view, wchar_t ch);
/* UTF-8 copy of the text; caller frees. */
char *composer_view_text(const ComposerView *view);
/* Text lines the input wants for a given composer width (1 to COMPOSER_MAX_ROWS). */
int   composer_view_rows_needed(const ComposerView *view, int composer_width, int has_buttons);
/* Replaces the word just before the caret when `convert` maps it (an
 * emoticon to an emoji). Returns 1 when it replaced something. */
int   composer_view_convert_word(ComposerView *view, const char *(*convert)(const char *word));
/* The emoji shortcode just before the caret: "(word" while it is typed
 * or "(word)" once closed, two letters or more (so "(y)" stays an emoticon), at the start of a word.
 * Copies `word` in lower case, sets where its "(" starts and whether it is
 * closed. Returns 1 when there is one. */
/* The word after an "@" that ends at the cursor (a mention being typed),
 * as UTF-8 in `out`, with the "@"'s position in *start. Returns 0 when the
 * cursor is not in one. */
int   composer_view_mention(const ComposerView *view, char *out, size_t size, int *start);
int   composer_view_shortcode(const ComposerView *view, char *out, size_t size, int *start, int *closed);
/* Replaces text[start..end) with `utf8` and puts the caret after it. Returns 1 on success. */
int   composer_view_replace(ComposerView *view, int start, int end, const char *utf8);
/* Scrolls the visible lines without moving the caret (mouse wheel). */
void  composer_view_scroll(ComposerView *view, int delta);
/* True when (y, x) is on ✕, which asks to clear what is typed. */
int   composer_view_hit_clear(const ComposerView *view, int y, int x);
int   composer_view_hit_emoji(const ComposerView *view, int y, int x);
int   composer_view_hit_attach(const ComposerView *view, int y, int x);
int   composer_view_hit_send(const ComposerView *view, int y, int x);
/* Draws the status line and the input; `chip` is an optional status such as
 * an attachment or a reply. Records where the caret belongs. */
void  composer_view_render(ComposerView *view, UiRect rect, int focused, int has_chat,
                           int recording_seconds, int enter_sends, const char *chip);

#endif
