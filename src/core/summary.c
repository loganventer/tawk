#include "core/summary.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

void summary_init(Summary *s) { memset(s, 0, sizeof(*s)); }

void summary_dispose(Summary *s) {
    if (!s) return;
    free(s->text);
    memset(s, 0, sizeof(*s));
}

void summary_set_text(Summary *s, const char *text) {
    free(s->text);
    s->text = text ? str_dup(text) : NULL;
}
