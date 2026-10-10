#ifndef APP_CLIENTS_TUI_TRANSCRIPT_VIEW_H
#define APP_CLIENTS_TUI_TRANSCRIPT_VIEW_H

#include <stddef.h>

#include "clients/tui/transcript_source.h"
#include "utilities/utf8_text.h"

/* The transcript of one voice note as the conversation shows it: its words
 * inside the voice note's own bubble, under the play line, in grey italics,
 * wrapped and cut to a few lines. The words are plain text: a model wrote
 * them from someone else's voice, so no WhatsApp formatting is applied.
 * `found` is 0 when there is nothing to show. */
typedef struct TranscriptView {
    int        found;
    Transcript transcript;
} TranscriptView;

/* Looks the message's transcript up through `source` (which may be NULL). */
void transcript_view_load(TranscriptView *view, const TranscriptSource *source, const Message *message, AccountId owner);
void transcript_view_dispose(TranscriptView *view);
/* The words wrapped to `cols`, at most `max_lines` of them; the caller frees
 * `*wrapped`. *cut is set when more follow. Returns how many lines are kept. */
int  transcript_view_wrap(const TranscriptView *view, int cols, int max_lines, TextLine **wrapped, int *cut);
/* One wrapped line, dimmed and in italics over `attr` (the bubble's colours);
 * `cut` ends it with an ellipsis, on the last line kept. */
void transcript_view_draw_line(const TranscriptView *view, int y, int x, int room, size_t offset, size_t length, int cut, int attr);
/* The same row for a soft-locked chat: its shape only. */
void transcript_view_draw_veiled(const TranscriptView *view, int y, int x, int room, size_t offset, size_t length, int attr);

#endif
