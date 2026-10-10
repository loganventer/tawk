/* The input line: stepping through what is typed a word at a time, and to
 * its ends, as Ctrl, Alt or Shift with the arrows do. */
#include "clients/tui/composer_view.h"

#include <locale.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

int main(void) {
    setlocale(LC_ALL, "");
    ComposerView v;
    composer_view_init(&v);
    composer_view_set_text(&v, "hello brave  new w\xC3\xAAreld");        /* "wêreld": one character is two bytes */
    CHECK(v.cursor == v.length && v.length == 23, "the cursor starts after what was typed");
    composer_view_move_word(&v, -1);
    CHECK(v.cursor == 17, "a step back lands on the start of the last word");
    composer_view_move_word(&v, -1);
    CHECK(v.cursor == 13, "and the next on the word before it, over more than one space");
    composer_view_move_word(&v, -1);
    composer_view_move_word(&v, -1);
    CHECK(v.cursor == 0, "down to the start");
    composer_view_move_word(&v, -1);
    CHECK(v.cursor == 0, "where another step back does nothing");
    composer_view_move_word(&v, 1);
    CHECK(v.cursor == 5, "a step forward lands after the first word");
    composer_view_move_word(&v, 1);
    CHECK(v.cursor == 11, "and the next after the second");
    composer_view_move_word(&v, 1);
    composer_view_move_word(&v, 1);
    CHECK(v.cursor == v.length, "up to the end");
    composer_view_move_word(&v, 1);
    CHECK(v.cursor == v.length, "where another step forward does nothing");
    composer_view_move_end(&v, -1);
    CHECK(v.cursor == 0, "the start of the text in one step");
    composer_view_move_end(&v, 1);
    CHECK(v.cursor == v.length, "and its end");
    composer_view_clear(&v);
    composer_view_move_word(&v, -1);
    composer_view_move_word(&v, 1);
    CHECK(v.cursor == 0, "an empty input stays where it is");

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: the input steps a word at a time and to its ends\n");
    return 0;
}
