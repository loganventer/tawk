#ifndef APP_CORE_APPROVAL_CARD_H
#define APP_CORE_APPROVAL_CARD_H

/* A waiting request as it is put to you in the owner's chat. */
typedef struct ApprovalCard {
    const char *client;        /* who asks */
    const char *action;        /* "send a message" */
    const char *chat_name;     /* where; "" when it is about no chat */
    const char *account_label; /* which number; "" with one account */
    const char *text;          /* the words, or NULL */
    int         editable;      /* other words in your answer change the text */
    int         changed;       /* this card reads back a text you changed */
    int         minutes_left;  /* until it is declined by itself */
} ApprovalCard;

#endif
