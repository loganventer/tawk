#ifndef APP_CORE_ACCOUNT_H
#define APP_CORE_ACCOUNT_H

#include <stdint.h>

#include "core/account_agent_access.h"
#include "core/account_id.h"

#define ACCOUNT_MAX         8      /* accounts one tawk holds at a time */
#define ACCOUNT_LABEL_SIZE  64     /* bytes: 24 characters of UTF-8, with room */
#define ACCOUNT_COLOURS     8      /* badge colours to tell accounts apart */

/* One of your WhatsApp accounts, as tawk keeps it. The label is yours and
 * can change; the id never does. jid and name are learned when it links. */
typedef struct Account {
    AccountId          id;
    char               label[ACCOUNT_LABEL_SIZE];
    char               jid[128];
    char               name[128];
    int                colour;         /* 0 to ACCOUNT_COLOURS - 1 */
    int                is_primary;     /* the account that sends when nothing else says which */
    AccountAgentAccess agent_access;
    int64_t            created_at;
} Account;

#endif
