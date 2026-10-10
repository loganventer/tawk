#ifndef APP_CORE_SUMMARY_H
#define APP_CORE_SUMMARY_H

#include <stdint.h>

#define SUMMARY_MAX_BYTES 2000

/* A long message in a few words, written by an agent's model for a chat in
 * TL;DR mode. It is a note beside the message, never part of it: the
 * original is always kept, and one key away. */
typedef struct Summary {
    char    message_id[64];
    char    model[48];        /* the model that wrote it, or "" */
    char    source[64];       /* the program that handed it over */
    int64_t created_at;
    char   *text;             /* owned, may be NULL */
} Summary;

void summary_init(Summary *summary);
/* Frees the text; the struct itself is not freed. */
void summary_dispose(Summary *summary);
void summary_set_text(Summary *summary, const char *text);

#endif
