#ifndef APP_CORE_AUTOMATION_SESSION_H
#define APP_CORE_AUTOMATION_SESSION_H

#include <stdint.h>

#include "core/control_origin.h"

/* One program connected to the control socket, as the Agents tab shows it. */
typedef struct AutomationSession {
    int           conn;
    char          client[64];
    char          doing[160];      /* what the agent says it is working on, in its own words */
    char          label[64];       /* what the program says tells it apart: its folder, how it is run */
    ControlOrigin origin;
    int64_t       since;           /* epoch seconds */
    int           requests;
    int           allowances;      /* "for this session" allowances you gave it */
    int           paused;          /* its writes are refused until you resume it */
    int           summariser;      /* your default agent: the one that writes TL;DR summaries */
} AutomationSession;

#endif
