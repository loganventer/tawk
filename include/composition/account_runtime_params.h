#ifndef APP_COMPOSITION_ACCOUNT_RUNTIME_PARAMS_H
#define APP_COMPOSITION_ACCOUNT_RUNTIME_PARAMS_H

#include <sqlite3.h>

#include "contracts/i_chat_exporter.h"
#include "contracts/i_notifier.h"
#include "core/settings.h"

/* What every account's runtime is built from: the parts all accounts share
 * and the choices made at start-up. */
typedef struct AccountRuntimeParams {
    sqlite3        *db;
    const Settings *settings;
    INotifier      *notifier;
    IChatExporter  *exporter;
    const char     *backend;          /* "whatsmeow" or "baileys", after --backend */
    const char     *sidecar_dir;      /* where the Node.js bridge was found */
    const char     *state_dir;        /* folder for backend logs */
    int             debug;
} AccountRuntimeParams;

#endif
