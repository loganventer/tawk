#ifndef APP_CLIENTS_TUI_MESSAGE_ROW_KIND_H
#define APP_CLIENTS_TUI_MESSAGE_ROW_KIND_H

typedef enum MessageRowKind {
    MESSAGE_ROW_DAY = 0,
    MESSAGE_ROW_SENDER,
    MESSAGE_ROW_MEDIA,
    MESSAGE_ROW_TEXT,
    MESSAGE_ROW_META,
    MESSAGE_ROW_GAP,
    MESSAGE_ROW_QUOTE,       /* "▎Name: quoted text" at the top of a reply */
    MESSAGE_ROW_THUMB,       /* one row of a picture preview */
    MESSAGE_ROW_REACTIONS,   /* (unused, reactions share the meta line) */
    MESSAGE_ROW_EDGE_TOP,    /* half-row padding above the bubble */
    MESSAGE_ROW_EDGE_BOTTOM, /* half-row padding below the bubble */
    MESSAGE_ROW_LINK_TITLE,  /* a link card: the page's title */
    MESSAGE_ROW_LINK_DESC,   /* a line of its description */
    MESSAGE_ROW_LINK_SITE,   /* the site it is on */
    MESSAGE_ROW_FORWARDED,   /* "↪ Forwarded" above a forwarded message */
    MESSAGE_ROW_SCHEDULED,   /* a line of a message you scheduled (message: index into the scheduled list) */
    MESSAGE_ROW_SCHEDULED_META, /* "🕓 Today 18:00" under it */
    MESSAGE_ROW_STATUS_THUMB,  /* one row of the picture of the status a reply answers */
    MESSAGE_ROW_STATUS_TEXT,   /* a line of that status's words (on its colour) or caption */
    MESSAGE_ROW_TLDR_HEAD,     /* "▸ TL;DR" over a long message's summary, or "▾ TL;DR" over the original: the line that folds it */
    MESSAGE_ROW_TLDR,          /* a line of the summary */
    MESSAGE_ROW_TRANSCRIPT     /* a line of a voice note's transcript, in its bubble under the play line */
} MessageRowKind;

#endif
