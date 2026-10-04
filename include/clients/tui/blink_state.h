#ifndef APP_CLIENTS_TUI_BLINK_STATE_H
#define APP_CLIENTS_TUI_BLINK_STATE_H

#include <stdint.h>
#include "core/account_id.h"

/* Which chat is blinking for a new message, and until when. */
typedef struct BlinkState {
    char      jid[128];
    AccountId account;      /* the account the message reached; ACCOUNT_ID_NONE when not said */
    int64_t   until_ms;
} BlinkState;

/* True during the visible half of the blink cycle. */
int blink_state_on(const BlinkState *blink, const char *jid, int64_t now_ms);
/* The same for one row of the chat list. `account` is the account the row
 * belongs to, or ACCOUNT_ID_NONE for a row that stands for several: someone
 * on two of your numbers and not merged blinks only where the message is. */
int blink_state_on_row(const BlinkState *blink, const char *jid, AccountId account, int64_t now_ms);

#endif
