#ifndef APP_CORE_TRANSCRIPT_H
#define APP_CORE_TRANSCRIPT_H

#include <stdint.h>

#define TRANSCRIPT_MAX_BYTES 16384

/* The words of a voice note, written out by a transcriber. It is a note
 * beside the message, never part of it: the sender did not type it. */
typedef struct Transcript {
    char    message_id[64];
    char    language[16];     /* the language it was written in, or "" when the transcriber did not say */
    char    model[32];        /* the model that wrote it, or "" */
    char    source[64];       /* the program that handed it over */
    int64_t created_at;
    char   *text;             /* owned, may be NULL */
} Transcript;

void transcript_init(Transcript *transcript);
/* Frees the text; the struct itself is not freed. */
void transcript_dispose(Transcript *transcript);
void transcript_set_text(Transcript *transcript, const char *text);
/* Deep copy; dst must be initialised or disposed. */
void transcript_copy(Transcript *dst, const Transcript *src);
void transcript_array_free(Transcript *items, int count);

#endif
