#ifndef APP_CLIENTS_TUI_MESSAGE_VIEW_H
#define APP_CLIENTS_TUI_MESSAGE_VIEW_H

#include "clients/tui/image_placement.h"
#include "clients/tui/message_row.h"
#include "clients/tui/message_view_context.h"
#include "clients/tui/quoted_status.h"
#include "clients/tui/summary_view.h"
#include "clients/tui/transcript_view.h"
#include "clients/tui/ui_rect.h"
#include "core/message.h"
#include "core/styled_text.h"

#define MESSAGE_VIEW_MAX_SCREEN_ROWS 512
#define MESSAGE_VIEW_MAX_PLACEMENTS  16
#define MESSAGE_VIEW_MAX_UNFOLDED    32

/* The conversation pane: bubbles, day separators, media and voice notes. */
typedef struct MessageView {
    int         scroll;          /* rows scrolled up from the newest message */
    int         selected;        /* message index, or -1 */
    int         follow_selection;  /* keep the selection on screen; off once the user scrolls */
    MessageRow *rows;
    int         row_count;
    int         row_cap;
    int         screen_rows[MESSAGE_VIEW_MAX_SCREEN_ROWS];   /* view row -> message index or -1 */
    unsigned char screen_quote[MESSAGE_VIEW_MAX_SCREEN_ROWS];  /* the row is a quote strip */
    short       screen_x0[MESSAGE_VIEW_MAX_SCREEN_ROWS];     /* the bubble's columns on that row */
    short       screen_x1[MESSAGE_VIEW_MAX_SCREEN_ROWS];
    UiRect      last_rect;
    UiRect      newer_button;    /* "↓ newer" badge, clickable when scrolled up */
    ImagePlacement placements[MESSAGE_VIEW_MAX_PLACEMENTS];   /* photos left blank for pixel images */
    int         header_rows;     /* the title bar: 1, or 2 with a portrait */
    UiRect      portrait_rect;   /* the title bar portrait, for clicks */
    UiRect      title_rect;      /* the name, for clicks */
    int         placement_count;
    int         thumb_cols;      /* the width pictures were sized for in the last layout, so drawing asks for the same one */
    StyledText *styled;          /* per message: its text as shown, formatted (text NULL when unformatted) */
    int         styled_count;
    QuotedStatus *quoted;        /* per message: the status it answers (found 0 when none) */
    int         quoted_count;
    TranscriptView *transcripts; /* per message: the transcript of a voice note (found 0 when none) */
    int         transcript_count;
    SummaryView *summaries;      /* per message: its TL;DR summary (found 0 when none) */
    int         summary_count;
    char        unfolded[MESSAGE_VIEW_MAX_UNFOLDED][64];   /* ids of summarised messages showing their original */
    int         unfolded_count;
    int         held;            /* the place below is put back at the next render */
    char        held_top[64];    /* id of the message at the top of the screen */
    int         held_offset;     /* which of its rows is the top row */
    char        held_selected[64];  /* id of the selected message, or empty */
    int         has_newer;       /* messages newer than the loaded ones exist */
    int         drawn;           /* set by a render that drew messages; the owner clears it */
} MessageView;

void message_view_init(MessageView *view);
void message_view_dispose(MessageView *view);

void message_view_render(MessageView *view, UiRect rect, const Message *messages, int count,
                         const MessageViewContext *ctx);
/* The first and last message on screen at the last render. Returns 0 when none is. */
int  message_view_visible_range(const MessageView *view, int *first, int *last);
/* Remembers what is on screen and selected by message id, so the place is kept
 * when the message array is about to change under the view (a page loaded or
 * let go). The next render puts it back; release drops it unused. */
void message_view_hold(MessageView *view, const Message *messages, int count);
void message_view_release(MessageView *view);
void message_view_scroll(MessageView *view, int delta);
void message_view_scroll_to_latest(MessageView *view);
/* Moves the selection by delta messages (selection starts at the newest). */
void message_view_select(MessageView *view, int count, int delta);
/* Whether message `index` has a TL;DR summary (shown or unfolded) at the last render. */
int  message_view_summarised(const MessageView *view, int index);
/* Whether a summarised message shows its original instead. */
int  message_view_unfolded(const MessageView *view, const char *message_id);
/* Unfolds a summarised message to its original, or folds it back to the summary. */
void message_view_toggle_summary(MessageView *view, const char *message_id);
/* Message whose bubble is under (y, x), or -1 (a click beside a bubble hits nothing). */
int  message_view_hit(MessageView *view, int y, int x);
/* The message whose quote strip is under (y, x), or -1. */
int  message_view_hit_quote(MessageView *view, int y, int x);
/* True when (y, x) is on the title bar portrait, or on the name. */
int  message_view_hit_portrait(const MessageView *view, int y, int x);
int  message_view_hit_title(const MessageView *view, int y, int x);
/* True when (y, x) is on the "↓ newer" badge. */
int  message_view_hit_newer(const MessageView *view, int y, int x);

#endif
