#ifndef APP_CORE_CHAT_ALERT_LEVEL_H
#define APP_CORE_CHAT_ALERT_LEVEL_H

/* Which messages of one chat alert you. */
typedef enum {
    CHAT_ALERT_ALL = 0,       /* every message, as the settings allow */
    CHAT_ALERT_MENTIONS       /* only one that mentions you */
} ChatAlertLevel;

#endif
