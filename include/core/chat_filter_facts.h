#ifndef APP_CORE_CHAT_FILTER_FACTS_H
#define APP_CORE_CHAT_FILTER_FACTS_H

/* What is known about a chat besides the chat itself, for narrowing the list. */
typedef struct ChatFilterFacts {
    int awaiting;     /* your last message there is unanswered */
    int snoozed;      /* it has a reminder that is not due */
    int labelled;     /* it carries the label asked for */
} ChatFilterFacts;

#endif
