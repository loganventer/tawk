#ifndef APP_CORE_LIVE_KIND_H
#define APP_CORE_LIVE_KIND_H

/* What just happened to a message, for readers that follow along. */
typedef enum LiveKind {
    LIVE_KIND_MESSAGE = 0,     /* it arrived, or you sent it */
    LIVE_KIND_READ,            /* someone read one you sent */
    LIVE_KIND_REACTION,        /* someone reacted to one you sent, or took a reaction back */
    LIVE_KIND_EDIT,            /* someone changed the words of one they sent */
    LIVE_KIND_DELETE,          /* someone deleted one they sent, for everyone */
    LIVE_KIND_SCHEDULED_SENT,  /* one you scheduled went out; the id is the scheduled message's */
    LIVE_KIND_MEDIA_READY,     /* its photo, voice note or file finished downloading */
    LIVE_KIND_PRESENCE         /* the person in a chat came online or left; there is no message id */
} LiveKind;

#endif
