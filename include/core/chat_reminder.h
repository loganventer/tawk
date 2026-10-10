#ifndef APP_CORE_CHAT_REMINDER_H
#define APP_CORE_CHAT_REMINDER_H

#include <stdint.h>

/* A chat put aside until a time, or until its person writes again. */
typedef struct ChatReminder {
    char    jid[128];
    int64_t due_at;        /* epoch seconds; 0: no time, only when they write */
    int64_t created_at;
} ChatReminder;

#endif
