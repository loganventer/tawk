#ifndef APP_CORE_AUTOMATION_ENTRY_H
#define APP_CORE_AUTOMATION_ENTRY_H

#include "core/account_id.h"
#include <stdint.h>

#include "core/automation_outcome.h"
#include "core/control_origin.h"

/* One line of the automation log: what a control socket client did or
 * tried, kept so you can check it later with /automation. */
typedef struct AutomationEntry {
    int64_t           at;              /* epoch seconds */
    ControlOrigin     origin;
    char              client[64];      /* the name it gave, such as "tawk-mcp" */
    char              op[32];
    char              chat_jid[128];
    char              summary[256];    /* the start of the text sent, or the emoji */
    AutomationOutcome outcome;
    AccountId         account;         /* which of your accounts it was about */
} AutomationEntry;

#endif
