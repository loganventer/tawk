#ifndef APP_MANAGERS_TRANSCRIPT_MANAGER_DEPS_H
#define APP_MANAGERS_TRANSCRIPT_MANAGER_DEPS_H

#include "contracts/i_chat_prefs_store.h"
#include "contracts/i_chat_transcript_prefs.h"
#include "contracts/i_transcript_store.h"
#include "core/settings.h"

/* What the transcript manager depends on, injected by the composition root. */
typedef struct TranscriptManagerDeps {
    ITranscriptStore     *store;      /* this account's transcripts */
    IChatPrefsStore      *prefs;      /* what you chose for each chat, read */
    IChatTranscriptPrefs *choices;    /* and set */
    const Settings       *settings;   /* the live settings */
} TranscriptManagerDeps;

#endif
