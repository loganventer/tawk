#ifndef APP_CORE_CHAT_LABEL_H
#define APP_CORE_CHAT_LABEL_H

#define CHAT_LABEL_SIZE 32      /* bytes, the name of a label with its end */

/* One label of yours on one person or group. Labels are kept on this computer only. */
typedef struct ChatLabel {
    char jid[128];
    char label[CHAT_LABEL_SIZE];
} ChatLabel;

#endif
