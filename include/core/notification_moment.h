#ifndef APP_CORE_NOTIFICATION_MOMENT_H
#define APP_CORE_NOTIFICATION_MOMENT_H

#include "core/chat_alert_level.h"

/* What the rules for alerting need to know besides the message and its chat. */
typedef struct NotificationMoment {
    int            live;            /* it arrived now, not from history */
    int            chat_is_open;    /* you are looking at its chat */
    ChatAlertLevel level;           /* the chat's own choice */
    int            weekday;         /* 0 Sunday to 6 Saturday, local time */
    int            minute_of_day;   /* local time */
} NotificationMoment;

#endif
