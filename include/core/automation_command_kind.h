#ifndef APP_CORE_AUTOMATION_COMMAND_KIND_H
#define APP_CORE_AUTOMATION_COMMAND_KIND_H

typedef enum AutomationCommandKind {
    AUTOMATION_COMMAND_DISCONNECT = 0,
    AUTOMATION_COMMAND_REVOKE,          /* forget its "for this session" allowances */
    AUTOMATION_COMMAND_PAUSE,
    AUTOMATION_COMMAND_RESUME,
    AUTOMATION_COMMAND_SUMMARISER       /* this one is your default agent (it writes TL;DR summaries), or stops being it */
} AutomationCommandKind;

#endif
