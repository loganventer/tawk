#ifndef APP_CORE_QUIET_HOURS_H
#define APP_CORE_QUIET_HOURS_H

/* A stretch of the day in which nothing alerts you, in minutes after midnight.
 * It may run over midnight (22:00 to 07:00). */
typedef struct QuietHours {
    int set;        /* 0: there are none */
    int from;       /* first quiet minute */
    int until;      /* first minute that is no longer quiet */
} QuietHours;

#endif
