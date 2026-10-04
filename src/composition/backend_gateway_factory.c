#include "composition/backend_gateway_factory.h"
#include "composition/account_runtime.h"
#include "resource_access/gateway_options.h"
#include "resource_access/sidecar_gateway.h"
#include "resource_access/whatsmeow_gateway.h"
#include "utilities/log.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The backend log of one account: the first keeps the file it always had. */
static void sidecar_log_path(const BackendGatewayOptions *o, AccountId id, char *out, size_t size) {
    char name[48];
    if (id <= ACCOUNT_ID_FIRST) str_copy(name, sizeof(name), "sidecar.log");
    else snprintf(name, sizeof(name), "sidecar-%d.log", id);
    path_join(out, size, o->state_dir, name);
}

static int backend_create(IGatewayFactory *self, AccountId id, EventQueue *events, GatewayParts *out) {
    const BackendGatewayOptions *o = self->ctx;
    GatewayOptions options;
    memset(&options, 0, sizeof(options));
    account_runtime_auth_dir(o->settings, id, options.auth_dir, sizeof(options.auth_dir));
    path_mkdir_p(options.auth_dir, 0700);
    str_copy(options.media_dir, sizeof(options.media_dir), o->settings->media_dir);
    str_copy(options.node_binary, sizeof(options.node_binary), o->settings->node_binary);
    sidecar_log_path(o, id, options.log_path, sizeof(options.log_path));
    str_copy(options.log_dir, sizeof(options.log_dir), o->state_dir);
    str_copy(options.sidecar_dir, sizeof(options.sidecar_dir), o->sidecar_dir);
    options.debug = o->debug;

    memset(out, 0, sizeof(*out));          /* no publisher on Baileys, no liker on whatsmeow */
    if (strcmp(o->backend, "baileys") != 0) {
        out->gateway = whatsmeow_gateway_create(&options, events);
        if (out->gateway) {
            out->backend_name = "whatsmeow (in-process)";
            out->editor = whatsmeow_gateway_profile_editor(out->gateway);
            out->publisher = whatsmeow_gateway_status_publisher(out->gateway);
            return 0;
        }
        LOG_WARN("built without whatsmeow; using the Node.js sidecar");
    }
    /* Baileys keeps its login in its own folder: its logout clears that
     * folder, which must never touch the whatsmeow login beside it. */
    GatewayOptions sidecar = options;
    path_join(sidecar.auth_dir, sizeof(sidecar.auth_dir), options.auth_dir, "baileys");
    path_mkdir_p(sidecar.auth_dir, 0700);
    out->gateway = sidecar_gateway_create(&sidecar, events);
    if (!out->gateway) return -1;
    out->backend_name = "baileys (Node.js sidecar)";
    out->editor = sidecar_gateway_profile_editor(out->gateway);
    out->liker = sidecar_gateway_status_liker(out->gateway);
    return 0;
}

static void backend_destroy(IGatewayFactory *self) { free(self); }

IGatewayFactory *backend_gateway_factory_create(const BackendGatewayOptions *options) {
    IGatewayFactory *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->ctx = (void *)options;
    f->create = backend_create;
    f->destroy = backend_destroy;
    return f;
}
