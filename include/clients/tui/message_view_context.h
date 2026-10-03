#ifndef APP_CLIENTS_TUI_MESSAGE_VIEW_CONTEXT_H
#define APP_CLIENTS_TUI_MESSAGE_VIEW_CONTEXT_H

#include "clients/tui/account_badge.h"
#include "clients/tui/name_resolver.h"
#include "core/scheduled_message.h"
#include "clients/tui/thumbnail_cache.h"
#include "clients/tui/media_sources.h"
#include "clients/tui/message_formatter.h"
#include "clients/tui/status_source.h"

/* Everything the view needs besides the messages. thumbs may be NULL when
 * previews are turned off. */
typedef struct MessageViewContext {
    const char         *title;          /* chat name */
    const char         *status;         /* title bar note such as "loading older messages…", or "" */
    int                 is_group;
    int                 focused;
    int                 use_24h;
    const char         *playing_path;   /* voice note playing now, or "" */
    int64_t             playing_ms;     /* how far into it */
    const NameResolver *names;
    ThumbnailCache     *thumbs;
    const MediaSources *media;          /* video frames and PDF pages; may be NULL */
    int                 pixel_images;   /* leave fully visible photos blank for a pixel (Sixel) image */
    int                 portrait_pixels;/* the same for the title bar portrait, which popups beside it leave uncovered */
    int                 veiled;         /* soft-locked chat: draw the shape of messages, not their content */
    /* Title bar portrait: shown when `jid` is set; `portrait` is a picture path or NULL (initials). */
    const char         *jid;
    const char         *portrait;
    const char         *subtitle;       /* second title line: the about text, or "" */
    const char         *activity;       /* "typing…", "Jan is recording audio…", shown above the input; or "" */
    int                 activity_phase; /* animation step for the dots */
    const MessageFormatter *formatter;  /* formatted text (*bold*, mentions); NULL shows the raw text */
    /* Messages you scheduled for this chat, soonest first: shown dimmed after the rest. */
    const ScheduledMessage *scheduled;
    int                 scheduled_count;
    /* The statuses replies answer (a reply to a status shows it); NULL shows only the quoted words. */
    const StatusSource *statuses;
    /* A chat merged across accounts: the account each message belongs to, and the badge of each account. NULL otherwise. */
    const AccountId    *owners;
    const AccountBadge *badges;
    int                 badge_count;
} MessageViewContext;

#endif
