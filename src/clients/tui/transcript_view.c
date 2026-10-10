#include "clients/tui/transcript_view.h"
#include "clients/tui/text_veil.h"
#include "clients/tui/tui_draw.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>

/* Grey italics where the terminal has italics (terminfo "sitm"); grey alone
 * elsewhere, since underlining several lines would be hard to read. */
static int spoken_attr(int base) {
    static int italic = -1;
    if (italic < 0) {
        const char *sitm = tigetstr("sitm");
        italic = sitm && sitm != (char *)-1 ? (int)A_ITALIC : 0;
    }
    return base | (int)A_DIM | italic;
}

void transcript_view_load(TranscriptView *v, const TranscriptSource *source, const Message *message, AccountId owner) {
    memset(v, 0, sizeof(*v));
    transcript_init(&v->transcript);
    if (!source || !source->find || !message) return;
    if (source->find(source->ctx, message, owner, &v->transcript) != 0 || !v->transcript.text || !v->transcript.text[0]) {
        transcript_dispose(&v->transcript);
        return;
    }
    v->found = 1;
}

void transcript_view_dispose(TranscriptView *v) {
    transcript_dispose(&v->transcript);
    memset(v, 0, sizeof(*v));
}

int transcript_view_wrap(const TranscriptView *v, int cols, TextLine **wrapped) {
    *wrapped = NULL;
    if (!v->found || cols < 1) return 0;
    return utf8_wrap(v->transcript.text, cols, wrapped);
}

void transcript_view_draw_line(const TranscriptView *v, int y, int x, int room, size_t offset, size_t length, int attr) {
    if (!v->found || offset + length > strlen(v->transcript.text)) return;
    tui_text_n(y, x, room, v->transcript.text + offset, length, spoken_attr(attr));
}

void transcript_view_draw_veiled(const TranscriptView *v, int y, int x, int room, size_t offset, size_t length, int attr) {
    if (!v->found || offset + length > strlen(v->transcript.text)) return;
    text_veil_text(y, x, room, v->transcript.text + offset, length, attr);
}
