#ifndef APP_CORE_CHAT_FILTER_KIND_H
#define APP_CORE_CHAT_FILTER_KIND_H

/* Which chats the list is narrowed to. */
typedef enum {
    CHAT_FILTER_NONE = 0,
    CHAT_FILTER_UNREAD,
    CHAT_FILTER_GROUPS,
    CHAT_FILTER_DIRECT,
    CHAT_FILTER_AWAITING,     /* your last message there has gone unanswered for some days */
    CHAT_FILTER_SNOOZED,      /* put aside with a reminder */
    CHAT_FILTER_LABEL         /* carrying one label */
} ChatFilterKind;

#endif
