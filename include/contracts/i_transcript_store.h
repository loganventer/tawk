#ifndef APP_CONTRACTS_I_TRANSCRIPT_STORE_H
#define APP_CONTRACTS_I_TRANSCRIPT_STORE_H

#include "core/transcript.h"

/* The transcripts of voice notes, one per message and language. */
typedef struct ITranscriptStore {
    void *ctx;
    /* Keeps `transcript`, replacing the one this message already has in that language. */
    int  (*save)(struct ITranscriptStore *self, const Transcript *transcript);
    /* Every transcript of a message, newest first. The caller frees `*out`
     * with transcript_array_free. Returns 0, with *count 0 when there is none. */
    int  (*find)(struct ITranscriptStore *self, const char *message_id, Transcript **out, int *count);
    int  (*remove)(struct ITranscriptStore *self, const char *message_id);
    void (*destroy)(struct ITranscriptStore *self);
} ITranscriptStore;

#endif
