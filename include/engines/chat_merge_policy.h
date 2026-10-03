#ifndef APP_ENGINES_CHAT_MERGE_POLICY_H
#define APP_ENGINES_CHAT_MERGE_POLICY_H

#include "core/chat_merge_choice.h"

/* Whether one contact's chats in several of your accounts show as one chat:
 * what was chosen for the contact, or the setting when nothing was. */
int chat_merge_policy_merges(int setting_on, ChatMergeChoice choice);

#endif
