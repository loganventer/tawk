#ifndef APP_CONTRACTS_I_SUMMARY_STORE_H
#define APP_CONTRACTS_I_SUMMARY_STORE_H

#include "core/summary.h"

/* The TL;DR summaries of long messages, one per message. */
typedef struct ISummaryStore {
    void *ctx;
    /* Keeps `summary`, replacing the one this message already has. */
    int  (*save)(struct ISummaryStore *self, const Summary *summary);
    /* Fills `out` (the caller disposes it) and returns 0, or -1 when the message has none. */
    int  (*find)(struct ISummaryStore *self, const char *message_id, Summary *out);
    int  (*remove)(struct ISummaryStore *self, const char *message_id);
    void (*destroy)(struct ISummaryStore *self);
} ISummaryStore;

#endif
