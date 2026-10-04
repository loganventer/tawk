#ifndef APP_COMPOSITION_BACKEND_GATEWAY_OPTIONS_H
#define APP_COMPOSITION_BACKEND_GATEWAY_OPTIONS_H

#include "core/settings.h"

/* The choices made at start-up that decide how an account reaches WhatsApp. */
typedef struct BackendGatewayOptions {
    const Settings *settings;
    const char     *backend;          /* "whatsmeow" or "baileys", after --backend */
    const char     *sidecar_dir;      /* where the Node.js bridge was found */
    const char     *state_dir;        /* folder for backend logs */
    int             debug;
} BackendGatewayOptions;

#endif
