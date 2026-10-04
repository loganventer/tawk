#ifndef APP_COMPOSITION_BACKEND_GATEWAY_FACTORY_H
#define APP_COMPOSITION_BACKEND_GATEWAY_FACTORY_H

#include "composition/backend_gateway_options.h"
#include "contracts/i_gateway_factory.h"

/* Makes real gateways: whatsmeow in-process, or the Node.js bridge when that
 * is asked for or whatsmeow was not built in. The options must outlive it. */
IGatewayFactory *backend_gateway_factory_create(const BackendGatewayOptions *options);

#endif
