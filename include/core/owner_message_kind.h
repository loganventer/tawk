#ifndef APP_CORE_OWNER_MESSAGE_KIND_H
#define APP_CORE_OWNER_MESSAGE_KIND_H

/* What a message in the owner's chat is to an agent. */
typedef enum {
    OWNER_MESSAGE_NOT = 0,   /* tawk sent it itself: an agent's answer, a card, a note */
    OWNER_MESSAGE_WORDS,     /* typed by you: your words to the agent */
    OWNER_MESSAGE_DATA       /* yours, but not your words: forwarded, quoted, a file, a voice note */
} OwnerMessageKind;

#endif
