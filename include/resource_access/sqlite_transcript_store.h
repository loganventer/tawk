#ifndef APP_RESOURCE_ACCESS_SQLITE_TRANSCRIPT_STORE_H
#define APP_RESOURCE_ACCESS_SQLITE_TRANSCRIPT_STORE_H

#include <sqlite3.h>

#include "contracts/i_transcript_store.h"
#include "core/account_id.h"

/* A store over the transcripts of one account's voice notes. */
ITranscriptStore *sqlite_transcript_store_create(sqlite3 *db, AccountId account);

#endif
