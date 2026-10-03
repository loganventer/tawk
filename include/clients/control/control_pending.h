#ifndef APP_CLIENTS_CONTROL_CONTROL_PENDING_H
#define APP_CLIENTS_CONTROL_CONTROL_PENDING_H

#include <stdint.h>

#include "cJSON.h"
#include "clients/control/control_execute.h"
#include "core/account_id.h"
#include "core/control_origin.h"
#include "core/write_kind.h"

/* A write a client asked for, checked and ready to carry out, kept while
 * it waits for confirmation or for your approval. */
typedef struct ControlPending {
    ControlExecute execute;
    WriteKind      kind;
    int            needs_connection;  /* only while connected to WhatsApp */
    int            editable;          /* you may change `text` before allowing it */
    int            edited;            /* you did */
    int            disclaimed;        /* the AI disclaimer was added under `text` */
    int            approval_id;       /* 0 until you are asked */
    int            conn;
    char           request_id[64];
    ControlOrigin  origin;
    char           client[64];
    char           op[32];
    char           chat_jid[128];     /* "" when it is about no chat */
    AccountId      account;           /* the account it was asked of, and is carried out by */
    char           action[128];       /* what you are asked: "delete the chat", "change Theme to dracula" */
    char          *text;              /* owned, may be NULL: words to send or post */
    cJSON         *args;              /* owned copy of the checked arguments */
    int64_t        expires_ms;
} ControlPending;

void control_pending_init(ControlPending *pending, const char *request_id, const char *op, WriteKind kind, ControlExecute execute);
void control_pending_dispose(ControlPending *pending);

#endif
