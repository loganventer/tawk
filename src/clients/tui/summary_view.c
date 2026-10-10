#include "clients/tui/summary_view.h"
#include "clients/tui/text_veil.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"

#include <string.h>

#define HEAD_FOLDED   "\xE2\x96\xB8 TL;DR"                        /* ▸ */
#define HEAD_UNFOLDED "\xE2\x96\xBE TL;DR \xC2\xB7 original"      /* ▾ */

void summary_view_load(SummaryView *v, const SummarySource *source, const Message *message, AccountId owner) {
    memset(v, 0, sizeof(*v));
    summary_init(&v->summary);
    if (!source || !source->find || !message) return;
    if (source->find(source->ctx, message, owner, &v->summary) != 0 || !v->summary.text || !v->summary.text[0]) {
        summary_dispose(&v->summary);
        return;
    }
    v->found = 1;
}

void summary_view_dispose(SummaryView *v) {
    summary_dispose(&v->summary);
    memset(v, 0, sizeof(*v));
}

const char *summary_view_head(int folded) { return folded ? HEAD_FOLDED : HEAD_UNFOLDED; }

void summary_view_draw_head(int folded, int y, int x, int room, int attr) {
    tui_text(y, x, room, summary_view_head(folded), attr | ATTR_DIM | ATTR_BOLD);
}

void summary_view_draw_line(const SummaryView *v, int y, int x, int room, size_t offset, size_t length, int attr) {
    if (!v->found || offset + length > strlen(v->summary.text)) return;
    tui_text_n(y, x, room, v->summary.text + offset, length, attr);
}

void summary_view_draw_veiled(const SummaryView *v, int y, int x, int room, size_t offset, size_t length, int attr) {
    if (!v->found || offset + length > strlen(v->summary.text)) return;
    text_veil_text(y, x, room, v->summary.text + offset, length, attr);
}
