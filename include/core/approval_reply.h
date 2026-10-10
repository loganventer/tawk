#ifndef APP_CORE_APPROVAL_REPLY_H
#define APP_CORE_APPROVAL_REPLY_H

/* What you answered a request with from WhatsApp. */
typedef enum {
    APPROVAL_REPLY_NONE = 0,   /* nothing that answers it */
    APPROVAL_REPLY_ALLOW,
    APPROVAL_REPLY_DECLINE,
    APPROVAL_REPLY_EDIT        /* other words: the text to send instead */
} ApprovalReply;

#endif
