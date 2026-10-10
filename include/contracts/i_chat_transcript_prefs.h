#ifndef APP_CONTRACTS_I_CHAT_TRANSCRIPT_PREFS_H
#define APP_CONTRACTS_I_CHAT_TRANSCRIPT_PREFS_H

#include "core/chat_transcript_choice.h"

/* What you chose about transcripts for one person or group. It is read with
 * the rest of ChatPrefs, through IChatPrefsStore; this sets it. Handed out
 * by the chat prefs store, which owns it. */
typedef struct IChatTranscriptPrefs {
    void *ctx;
    int  (*set_show)(struct IChatTranscriptPrefs *self, const char *jid, ChatTranscriptChoice choice);
    /* off 1: nothing in this chat is transcribed from now on. */
    int  (*set_transcribe_off)(struct IChatTranscriptPrefs *self, const char *jid, int off);
} IChatTranscriptPrefs;

#endif
