#ifndef APP_CLIENTS_TUI_TRANSCRIPT_SOURCE_H
#define APP_CLIENTS_TUI_TRANSCRIPT_SOURCE_H

#include "core/account_id.h"
#include "core/message.h"
#include "core/transcript.h"

/* Where the conversation finds the transcript of a voice note, without
 * knowing who keeps them or which language you prefer. `find` fills `out`
 * (the caller disposes it) and returns -1 when the message has none.
 * `owner` is the account the message belongs to in a chat merged across
 * accounts, ACCOUNT_ID_NONE anywhere else. */
typedef struct TranscriptSource {
    void *ctx;
    int  (*find)(void *ctx, const Message *message, AccountId owner, Transcript *out);
} TranscriptSource;

#endif
