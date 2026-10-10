#ifndef APP_COMPOSITION_ACCOUNT_RUNTIME_PARAMS_H
#define APP_COMPOSITION_ACCOUNT_RUNTIME_PARAMS_H

#include <sqlite3.h>

#include "contracts/i_chat_exporter.h"
#include "contracts/i_chat_prefs_store.h"
#include "contracts/i_chat_summary_prefs.h"
#include "contracts/i_chat_transcript_prefs.h"
#include "contracts/i_gateway_factory.h"
#include "contracts/i_notifier.h"
#include "core/settings.h"

/* What every account's runtime is built from: the parts all accounts share,
 * and the factory that connects each one to WhatsApp. */
typedef struct AccountRuntimeParams {
    sqlite3        *db;
    const Settings *settings;
    INotifier      *notifier;
    IChatExporter  *exporter;
    IGatewayFactory *gateways;
    /* What you chose for each chat, shared by every account. */
    IChatPrefsStore *chat_prefs;
    IChatTranscriptPrefs *transcript_prefs;
    IChatSummaryPrefs *summary_prefs;
} AccountRuntimeParams;

#endif
