#ifndef APP_MANAGERS_OWNER_CHAT_MANAGER_H
#define APP_MANAGERS_OWNER_CHAT_MANAGER_H

#include <stdint.h>

#include "core/account_id.h"
#include "core/message.h"
#include "core/owner_message_kind.h"
#include "core/sent_kind.h"
#include "managers/owner_chat_manager_deps.h"

/* The owner's chat: the "message yourself" chat of one of your numbers,
 * where what you type reaches a connected agent as your words, the agent
 * answers you by itself, and waiting requests are put to you. This manager
 * knows which account that is, what tawk sent into the chat, and how many
 * answers went out this hour. The control client does the sending. */
typedef struct OwnerChatManager OwnerChatManager;

OwnerChatManager *owner_chat_manager_create(const OwnerChatManagerDeps *deps);
void              owner_chat_manager_destroy(OwnerChatManager *mgr);

/* The account whose "message yourself" chat it is, or ACCOUNT_ID_NONE while none is named. */
AccountId owner_chat_manager_account(OwnerChatManager *mgr);
/* Whether `chat_jid`, in `account` whose own JID is `own_jid`, is the owner's chat. */
int  owner_chat_manager_is(OwnerChatManager *mgr, AccountId account, const char *own_jid, const char *chat_jid);
/* Remembers a message tawk put there, and says whether it did. */
void owner_chat_manager_note_sent(OwnerChatManager *mgr, AccountId account, const char *message_id, SentKind kind, int ref);
int  owner_chat_manager_sent(OwnerChatManager *mgr, AccountId account, const char *message_id, SentKind *kind, int *ref);
/* What a message that arrived in the owner's chat is to an agent. */
OwnerMessageKind owner_chat_manager_kind(OwnerChatManager *mgr, AccountId account, const Message *msg);
/* Whether one more answer may go out unasked now; a yes counts against this hour. */
int  owner_chat_manager_take_reply(OwnerChatManager *mgr, int64_t now_ms);
/* How long a request waits in tawk before it is put to you on WhatsApp, or -1 when it never is. */
int64_t owner_chat_manager_card_wait_ms(OwnerChatManager *mgr);

#endif
