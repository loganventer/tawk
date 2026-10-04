#ifndef APP_CORE_APPROVAL_REQUEST_H
#define APP_CORE_APPROVAL_REQUEST_H

#include <stdint.h>

#include "core/account_id.h"
#include "core/approval_risk.h"
#include "core/control_origin.h"

/* Something a control socket client wants to do in your name, waiting for
 * you to allow or decline it. */
typedef struct ApprovalRequest {
    int           id;
    ControlOrigin origin;
    char          client[64];
    char          op[32];            /* send_message, react, schedule_message, mark_read */
    char          chat_jid[128];
    char          chat_name[128];
    AccountId     account;           /* which of your accounts it would be done with */
    char          account_label[64]; /* "" while agents may use one account only */
    char          action[96];        /* "send a message", "react with 👍", "send at Fri 17:30" */
    char         *text;              /* owned: the words to send, may be NULL */
    int           editable;          /* you may change the text before allowing it */
    int           danger;            /* it deletes or blocks: shown as a warning, Cancel first */
    ApprovalRisk  risk;
    int64_t       asked_ms;          /* when it arrived */
    int64_t       expires_ms;        /* declined by itself after this */
} ApprovalRequest;

void approval_request_copy(ApprovalRequest *dst, const ApprovalRequest *src);
void approval_request_dispose(ApprovalRequest *request);

#endif
