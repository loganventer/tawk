#ifndef APP_MANAGERS_ACCOUNT_ROSTER_MANAGER_H
#define APP_MANAGERS_ACCOUNT_ROSTER_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include "core/account.h"
#include "core/chat_prefs.h"
#include "managers/account_roster_manager_deps.h"

/* Your accounts as a list: adding, naming and removing them, which one is
 * primary, what agents may do with each, and what you chose for a contact
 * across them. It connects nothing; the runtime of an account is made and
 * taken down by whoever holds them, after asking here. */
typedef struct AccountRosterManager AccountRosterManager;

AccountRosterManager *account_roster_manager_create(const AccountRosterManagerDeps *deps);
void                  account_roster_manager_destroy(AccountRosterManager *mgr);

/* In id order. Returns how many were written. */
int  account_roster_manager_list(AccountRosterManager *mgr, Account *out, int max);
int  account_roster_manager_get(AccountRosterManager *mgr, AccountId id, Account *out);
/* The primary account's id, or ACCOUNT_ID_NONE when there are no accounts. */
AccountId account_roster_manager_primary(AccountRosterManager *mgr);

/* Each returns 0, or -1 with the reason for people in account_roster_manager_error. */
int  account_roster_manager_add(AccountRosterManager *mgr, const char *label, int64_t now, AccountId *id_out);
int  account_roster_manager_rename(AccountRosterManager *mgr, AccountId id, const char *label);
int  account_roster_manager_set_primary(AccountRosterManager *mgr, AccountId id);
int  account_roster_manager_set_agent_access(AccountRosterManager *mgr, AccountId id, AccountAgentAccess access);
/* Removes the account and everything kept for it. The last account stays;
 * a removed primary hands that on to the oldest account left. */
int  account_roster_manager_remove(AccountRosterManager *mgr, AccountId id);
const char *account_roster_manager_error(const AccountRosterManager *mgr);

/* What an account learns about itself and remembers between runs. */
int  account_roster_manager_linked(AccountRosterManager *mgr, AccountId id, const char *jid, const char *name);
int  account_roster_manager_set_last_chat(AccountRosterManager *mgr, AccountId id, const char *jid);
int  account_roster_manager_last_chat(AccountRosterManager *mgr, AccountId id, char *out, size_t size);
int  account_roster_manager_set_self_approval_chats(AccountRosterManager *mgr, AccountId id, const char *chats);
int  account_roster_manager_self_approval_chats(AccountRosterManager *mgr, AccountId id, char *out, size_t size);

/* What you chose for one contact, whichever account they are on. */
int  account_roster_manager_chat_prefs(AccountRosterManager *mgr, const char *jid, ChatPrefs *out);
/* ACCOUNT_ID_NONE puts the contact back on the primary account. */
int  account_roster_manager_set_send_account(AccountRosterManager *mgr, const char *jid, AccountId account);
int  account_roster_manager_set_merge(AccountRosterManager *mgr, const char *jid, ChatMergeChoice merge);
/* Everyone with a sending account of their own. Returns how many. */
int  account_roster_manager_send_accounts(AccountRosterManager *mgr, ChatPrefs *out, int max);

/* True once since the last call when the list or anything in it changed. */
int  account_roster_manager_take_changed(AccountRosterManager *mgr);

#endif
