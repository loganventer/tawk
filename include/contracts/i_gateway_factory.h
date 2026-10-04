#ifndef APP_CONTRACTS_I_GATEWAY_FACTORY_H
#define APP_CONTRACTS_I_GATEWAY_FACTORY_H

#include "contracts/gateway_parts.h"
#include "core/account_id.h"
#include "utilities/event_queue.h"

/* Makes the connection to WhatsApp for one account. Each account gets its
 * own, with its own login, pushing what happens onto that account's queue. */
typedef struct IGatewayFactory IGatewayFactory;

struct IGatewayFactory {
    void *ctx;
    /* Fills `out` and returns 0, or returns -1 when no gateway could be made. */
    int  (*create)(IGatewayFactory *self, AccountId account, EventQueue *events, GatewayParts *out);
    void (*destroy)(IGatewayFactory *self);
};

#endif
