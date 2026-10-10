#ifndef APP_CLIENTS_CONTROL_CONTROL_SUMMARY_WAIT_H
#define APP_CLIENTS_CONTROL_CONTROL_SUMMARY_WAIT_H

#include <stdint.h>

#include "core/account_id.h"

/* A summary a client was asked for and has not handed back yet. */
typedef struct ControlSummaryWait {
    char      message_id[64];
    AccountId account;
    int64_t   asked_ms;
} ControlSummaryWait;

#endif
