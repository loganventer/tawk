#ifndef APP_CORE_SENT_KIND_H
#define APP_CORE_SENT_KIND_H

/* What a message tawk itself put into the owner's chat was. */
typedef enum {
    SENT_KIND_REPLY = 0,    /* an agent's answer to you */
    SENT_KIND_CARD,         /* a request waiting for your answer; its ref is the request */
    SENT_KIND_NOTE          /* a line of tawk's own: a question, or what became of a request */
} SentKind;

#endif
