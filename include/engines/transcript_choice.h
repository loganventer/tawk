#ifndef APP_ENGINES_TRANSCRIPT_CHOICE_H
#define APP_ENGINES_TRANSCRIPT_CHOICE_H

#include "core/transcript.h"

/* Which of a voice note's transcripts is shown: the one in the earliest of
 * the languages you listed ("af,en"), else the first of `items` (the store
 * gives the newest first). Returns its index, or -1 when there is none. */
int transcript_choice_pick(const Transcript *items, int count, const char *languages);

#endif
