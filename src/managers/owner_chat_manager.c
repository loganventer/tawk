#include "managers/owner_chat_manager.h"
#include "engines/owner_message_rule.h"
#include "engines/owner_reply_rule.h"
#include "engines/self_chat_rule.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REPLIES_KEPT 512
#define SENT_KEPT_S  (30 * 24 * 60 * 60)     /* ids older than this cannot be answered or echoed any more */

struct OwnerChatManager {
    OwnerChatManagerDeps deps;
    int64_t replies[REPLIES_KEPT];           /* when the last answers went out, a ring */
    int     reply_count;
    int     reply_next;
    int     pruned;
};

OwnerChatManager *owner_chat_manager_create(const OwnerChatManagerDeps *deps) {
    if (!deps || !deps->sent || !deps->settings) return NULL;
    OwnerChatManager *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->deps = *deps;
    return m;
}

void owner_chat_manager_destroy(OwnerChatManager *m) { free(m); }

AccountId owner_chat_manager_account(OwnerChatManager *m) {
    const char *named = m->deps.settings->owner_chat;
    if (!named[0]) return ACCOUNT_ID_NONE;
    int id = atoi(named);
    return id > 0 ? id : ACCOUNT_ID_NONE;
}

int owner_chat_manager_is(OwnerChatManager *m, AccountId account, const char *own_jid, const char *chat_jid) {
    AccountId named = owner_chat_manager_account(m);
    return named != ACCOUNT_ID_NONE && named == account && self_chat_rule_is(own_jid, chat_jid);
}

void owner_chat_manager_note_sent(OwnerChatManager *m, AccountId account, const char *message_id, SentKind kind, int ref) {
    int64_t now = (int64_t)time(NULL);
    if (!m->pruned) { m->deps.sent->prune(m->deps.sent, now - SENT_KEPT_S); m->pruned = 1; }
    m->deps.sent->note(m->deps.sent, account, message_id, kind, ref, now);
}

int owner_chat_manager_sent(OwnerChatManager *m, AccountId account, const char *message_id, SentKind *kind, int *ref) {
    return m->deps.sent->find(m->deps.sent, account, message_id, kind, ref) == 0;
}

OwnerMessageKind owner_chat_manager_kind(OwnerChatManager *m, AccountId account, const Message *msg) {
    if (!msg) return OWNER_MESSAGE_NOT;
    return owner_message_rule_kind(msg, owner_chat_manager_sent(m, account, msg->id, NULL, NULL));
}

int owner_chat_manager_take_reply(OwnerChatManager *m, int64_t now_ms) {
    if (!owner_reply_rule_room(m->replies, m->reply_count, m->deps.settings->owner_replies_per_hour, now_ms)) return 0;
    m->replies[m->reply_next] = now_ms;
    m->reply_next = (m->reply_next + 1) % REPLIES_KEPT;
    if (m->reply_count < REPLIES_KEPT) m->reply_count++;
    return 1;
}

int64_t owner_chat_manager_card_wait_ms(OwnerChatManager *m) {
    if (!m->deps.settings->owner_approvals) return -1;
    return (int64_t)m->deps.settings->owner_card_wait * 1000;
}
