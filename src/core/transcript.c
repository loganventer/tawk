#include "core/transcript.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

void transcript_init(Transcript *t) { memset(t, 0, sizeof(*t)); }

void transcript_dispose(Transcript *t) {
    if (!t) return;
    free(t->text);
    memset(t, 0, sizeof(*t));
}

void transcript_set_text(Transcript *t, const char *text) {
    free(t->text);
    t->text = text ? str_dup(text) : NULL;
}

void transcript_copy(Transcript *dst, const Transcript *src) {
    char *text = src->text ? str_dup(src->text) : NULL;
    free(dst->text);
    *dst = *src;
    dst->text = text;
}

void transcript_array_free(Transcript *items, int count) {
    if (!items) return;
    for (int i = 0; i < count; i++) transcript_dispose(&items[i]);
    free(items);
}
