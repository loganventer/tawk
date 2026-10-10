#include "managers/reminder_manager.h"
#include "engines/reminder_rule.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>

#define REMINDERS_MAX 256

struct ReminderManager {
    IReminderStore *store;
    ChatReminder    rows[REMINDERS_MAX];
    int             count;
    int             loaded;
};

ReminderManager *reminder_manager_create(IReminderStore *store) {
    if (!store) return NULL;
    ReminderManager *m = calloc(1, sizeof(*m));
    if (m) m->store = store;
    return m;
}

void reminder_manager_destroy(ReminderManager *m) { free(m); }

static void load(ReminderManager *m) {
    if (m->loaded) return;
    int n = m->store->list(m->store, m->rows, REMINDERS_MAX);
    m->count = n > 0 ? n : 0;
    m->loaded = 1;
}

static int find(ReminderManager *m, const char *jid) {
    load(m);
    for (int i = 0; jid && i < m->count; i++) if (strcmp(m->rows[i].jid, jid) == 0) return i;
    return -1;
}

int reminder_manager_set(ReminderManager *m, const char *jid, int64_t due_at, int64_t now) {
    if (!jid || !jid[0]) return -1;
    load(m);
    if (find(m, jid) < 0 && m->count >= REMINDERS_MAX) return -1;
    ChatReminder r;
    memset(&r, 0, sizeof(r));
    str_copy(r.jid, sizeof(r.jid), jid);
    r.due_at = due_at;
    r.created_at = now;
    if (m->store->set(m->store, &r) != 0) return -1;
    m->loaded = 0;
    return 0;
}

int reminder_manager_clear(ReminderManager *m, const char *jid) {
    if (find(m, jid) < 0) return 0;
    if (m->store->clear(m->store, jid) != 0) return -1;
    m->loaded = 0;
    return 0;
}

int reminder_manager_snoozed(ReminderManager *m, const char *jid, int64_t *due_at) {
    int at = find(m, jid);
    if (at >= 0 && due_at) *due_at = m->rows[at].due_at;
    return at >= 0;
}

int reminder_manager_count(ReminderManager *m) { load(m); return m->count; }

const ChatReminder *reminder_manager_at(ReminderManager *m, int index) {
    load(m);
    return index >= 0 && index < m->count ? &m->rows[index] : NULL;
}

int reminder_manager_take_due(ReminderManager *m, const char *jid, int unread, int64_t now) {
    int at = find(m, jid);
    if (at < 0 || !reminder_rule_due(&m->rows[at], now, unread)) return 0;
    return reminder_manager_clear(m, jid) == 0;
}
