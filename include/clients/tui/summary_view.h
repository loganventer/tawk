#ifndef APP_CLIENTS_TUI_SUMMARY_VIEW_H
#define APP_CLIENTS_TUI_SUMMARY_VIEW_H

#include <stddef.h>

#include "clients/tui/summary_source.h"

/* The TL;DR of one long message as the conversation shows it: a line that
 * folds and unfolds ("▸ TL;DR" with the summary under it, or "▾ TL;DR" with
 * the original), and the summary's words. `found` is 0 when there is none. */
typedef struct SummaryView {
    int     found;
    Summary summary;
} SummaryView;

/* Looks the message's summary up through `source` (which may be NULL). */
void        summary_view_load(SummaryView *view, const SummarySource *source, const Message *message, AccountId owner);
void        summary_view_dispose(SummaryView *view);
/* The fold line: folded shows the summary, unfolded the original. */
const char *summary_view_head(int folded);
void        summary_view_draw_head(int folded, int y, int x, int room, int attr);
/* One wrapped line of the summary. */
void        summary_view_draw_line(const SummaryView *view, int y, int x, int room, size_t offset, size_t length, int attr);
/* The same row for a soft-locked chat: its shape only. */
void        summary_view_draw_veiled(const SummaryView *view, int y, int x, int room, size_t offset, size_t length, int attr);

#endif
