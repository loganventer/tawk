#include "managers/label_manager.h"
#include "engines/label_name.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROWS_MAX 2048

struct LabelManager {
    ILabelStore *store;
    ChatLabel   *rows;          /* every label on every chat, in label order */
    int          count;
    int          loaded;
};

LabelManager *label_manager_create(ILabelStore *store) {
    if (!store) return NULL;
    LabelManager *m = calloc(1, sizeof(*m));
    if (m) m->rows = calloc(ROWS_MAX, sizeof(ChatLabel));
    if (!m || !m->rows) { free(m); return NULL; }
    m->store = store;
    return m;
}

void label_manager_destroy(LabelManager *m) {
    if (!m) return;
    free(m->rows);
    free(m);
}

static void load(LabelManager *m) {
    if (m->loaded) return;
    int n = m->store->list(m->store, m->rows, ROWS_MAX);
    m->count = n > 0 ? n : 0;
    m->loaded = 1;
}

int label_manager_has(LabelManager *m, const char *jid, const char *label) {
    load(m);
    for (int i = 0; jid && label && i < m->count; i++) {
        if (strcmp(m->rows[i].label, label) == 0 && strcmp(m->rows[i].jid, jid) == 0) return 1;
    }
    return 0;
}

int label_manager_toggle(LabelManager *m, const char *jid, const char *label, char clean[CHAT_LABEL_SIZE]) {
    if (!jid || !jid[0] || label_name_clean(label, clean, CHAT_LABEL_SIZE) != 0) return -1;
    int on = !label_manager_has(m, jid, clean);
    int rc = on ? m->store->add(m->store, jid, clean) : m->store->remove(m->store, jid, clean);
    if (rc != 0) return -1;
    m->loaded = 0;
    return on;
}

void label_manager_of(LabelManager *m, const char *jid, char *out, size_t size) {
    load(m);
    size_t used = 0;
    if (size) out[0] = '\0';
    for (int i = 0; jid && i < m->count && used < size; i++) {
        if (strcmp(m->rows[i].jid, jid) != 0) continue;
        used += (size_t)snprintf(out + used, size - used, "%s%s", used ? ", " : "", m->rows[i].label);
    }
}

int label_manager_all(LabelManager *m, char out[][CHAT_LABEL_SIZE], int max) {
    load(m);
    int n = 0;
    for (int i = 0; i < m->count && n < max; i++) {
        if (n > 0 && strcmp(out[n - 1], m->rows[i].label) == 0) continue;    /* in label order: the same one follows itself */
        str_copy(out[n++], CHAT_LABEL_SIZE, m->rows[i].label);
    }
    return n;
}
