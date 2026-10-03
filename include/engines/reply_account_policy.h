#ifndef APP_ENGINES_REPLY_ACCOUNT_POLICY_H
#define APP_ENGINES_REPLY_ACCOUNT_POLICY_H

#include "engines/reply_account_candidate.h"

/* Which of your accounts sends to a contact. The first of these that names
 * an account which has the chat wins:
 *   1. for_message: the account picked for the message being written
 *   2. for_contact: the contact's own sending account
 *   3. primary:     your primary account
 *   4. the account whose chat with them has the newest message
 * ACCOUNT_ID_NONE for any of the first three means nothing was chosen.
 * Returns ACCOUNT_ID_NONE when no account has the chat. */
AccountId reply_account_policy_choose(AccountId for_message, AccountId for_contact, AccountId primary,
                                      const ReplyAccountCandidate *candidates, int count);

#endif
