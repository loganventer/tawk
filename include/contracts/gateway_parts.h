#ifndef APP_CONTRACTS_GATEWAY_PARTS_H
#define APP_CONTRACTS_GATEWAY_PARTS_H

#include "contracts/i_message_gateway.h"
#include "contracts/i_profile_editor.h"
#include "contracts/i_status_liker.h"
#include "contracts/i_status_publisher.h"

/* One account's connection to WhatsApp and the further things its backend
 * can do. The views belong to the gateway and go when it is destroyed. */
typedef struct GatewayParts {
    IMessageGateway  *gateway;
    IProfileEditor   *editor;        /* NULL when the backend cannot change your profile */
    IStatusPublisher *publisher;     /* NULL when it cannot post statuses */
    IStatusLiker     *liker;         /* NULL when it cannot like a status privately */
    const char       *backend_name;  /* shown in Settings, such as "whatsmeow (in-process)" */
} GatewayParts;

#endif
