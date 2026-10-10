#ifndef APP_MANAGERS_SUMMARY_MANAGER_DEPS_H
#define APP_MANAGERS_SUMMARY_MANAGER_DEPS_H

#include "contracts/i_chat_prefs_store.h"
#include "contracts/i_chat_summary_prefs.h"
#include "contracts/i_message_store.h"
#include "contracts/i_summary_store.h"
#include "core/settings.h"

/* What the summary manager depends on, injected by the composition root. */
typedef struct SummaryManagerDeps {
    ISummaryStore     *store;      /* this account's summaries */
    IChatPrefsStore   *prefs;      /* what you chose for each chat, read */
    IChatSummaryPrefs *choices;    /* and set */
    const Settings    *settings;   /* the live settings */
    IMessageStore     *messages;   /* this account's messages, to find the older ones of a TL;DR chat; NULL: only what is looked at */
} SummaryManagerDeps;

#endif
