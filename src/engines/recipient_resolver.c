#include "engines/recipient_resolver.h"
#include "engines/automation_policy.h"
#include "engines/recipient_reference_parser.h"
#include "utilities/str_util.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

typedef int (*NameMatcher)(const char *name, const char *ref);

static int whole(const char *name, const char *ref) { return name[0] && strcasecmp(name, ref) == 0; }

static int word_start(const char *name, const char *ref) {
    size_t n = strlen(ref);
    for (const char *p = name; *p; p++) {
        int starts = p == name || isspace((unsigned char)p[-1]) || p[-1] == '(' || p[-1] == '-';
        if (starts && strncasecmp(p, ref, n) == 0) return 1;
    }
    return 0;
}

static int has_chat(const Chat *chats, int count, const char *jid) {
    for (int i = 0; i < count; i++) if (strcmp(chats[i].jid, jid) == 0) return 1;
    return 0;
}

/* The chat this person's first message would start, as the policy would see it. */
static int allowed(const Settings *s, const Chat *chats, int count, const Recipient *who) {
    if (!recipient_reference_is_person(who->jid) || has_chat(chats, count, who->jid)) return 0;
    Chat c;
    chat_init(&c, who->jid);
    str_copy(c.name, sizeof(c.name), who->name);
    c.is_locked = 0;
    c.is_archived = 0;
    return automation_policy_chat_allowed(s, &c);
}

static void named(Recipient *who, const Contact *contacts, int count, const char *jid) {
    memset(who, 0, sizeof(*who));
    str_copy(who->jid, sizeof(who->jid), jid);
    for (int i = 0; i < count; i++) {
        if (strcmp(contacts[i].jid, jid) == 0) { contact_display_name(&contacts[i], who->name, sizeof(who->name)); return; }
    }
    contact_phone_from_jid(jid, who->name, sizeof(who->name));
}

static int listed(const Recipient *list, int count, const char *jid) {
    for (int i = 0; i < count; i++) if (strcmp(list[i].jid, jid) == 0) return 1;
    return 0;
}

ChatResolution recipient_resolve(const Settings *s, const Chat *chats, int chat_count,
                                 const Contact *contacts, int contact_count, const char *ref,
                                 Recipient *found, Recipient *candidates, int max, int *candidate_count) {
    static const NameMatcher MATCHERS[] = { whole, word_start };
    if (candidate_count) *candidate_count = 0;
    if (!ref || !*ref || !found) return CHAT_RESOLUTION_NOT_FOUND;

    char jid[128];
    if (recipient_reference_parse(ref, jid, sizeof(jid)) == 0) {
        named(found, contacts, contact_count, jid);
        return allowed(s, chats, chat_count, found) ? CHAT_RESOLUTION_FOUND : CHAT_RESOLUTION_NOT_FOUND;
    }

    for (size_t m = 0; m < sizeof(MATCHERS) / sizeof(MATCHERS[0]); m++) {
        Recipient hits[8];
        int n = 0;
        for (int i = 0; i < contact_count; i++) {
            const Contact *c = &contacts[i];
            if (!MATCHERS[m](c->name, ref) && !MATCHERS[m](c->push_name, ref)) continue;
            if (listed(hits, n, c->jid)) continue;
            Recipient who;
            named(&who, contacts, contact_count, c->jid);
            if (!allowed(s, chats, chat_count, &who)) continue;
            if (n < (int)(sizeof(hits) / sizeof(hits[0]))) hits[n] = who;
            n++;
        }
        if (n == 1) { *found = hits[0]; return CHAT_RESOLUTION_FOUND; }
        if (n > 1) {
            int kept = n < (int)(sizeof(hits) / sizeof(hits[0])) ? n : (int)(sizeof(hits) / sizeof(hits[0]));
            for (int i = 0; candidates && candidate_count && i < kept && i < max; i++) candidates[(*candidate_count)++] = hits[i];
            return CHAT_RESOLUTION_AMBIGUOUS;
        }
    }
    return CHAT_RESOLUTION_NOT_FOUND;
}
