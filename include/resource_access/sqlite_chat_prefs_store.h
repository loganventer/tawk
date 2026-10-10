#ifndef APP_RESOURCE_ACCESS_SQLITE_CHAT_PREFS_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_CHAT_PREFS_STORE_H

#include <sqlite3.h>

#include "contracts/i_chat_prefs_store.h"
#include "contracts/i_chat_summary_prefs.h"
#include "contracts/i_chat_transcript_prefs.h"

IChatPrefsStore *sqlite_chat_prefs_store_create(sqlite3 *db);
/* The transcript choices of the same table, as their own contract. Owned by
 * the store: it goes when the store is destroyed. */
IChatTranscriptPrefs *sqlite_chat_prefs_store_transcripts(IChatPrefsStore *store);
/* And the TL;DR switch, the same way. */
IChatSummaryPrefs *sqlite_chat_prefs_store_summaries(IChatPrefsStore *store);

#endif
