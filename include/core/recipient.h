#ifndef APP_CORE_RECIPIENT_H
#define APP_CORE_RECIPIENT_H

/* Someone a message can go to who has no chat yet: the first message starts the chat. */
typedef struct Recipient {
    char jid[128];
    char name[128];   /* the address-book name, else the name they chose, else "+<number>" */
} Recipient;

#endif
