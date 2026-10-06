#ifndef APP_ENGINES_RECIPIENT_RESOLVER_H
#define APP_ENGINES_RECIPIENT_RESOLVER_H

#include "core/chat.h"
#include "core/chat_resolution.h"
#include "core/contact.h"
#include "core/recipient.h"
#include "core/settings.h"

/* Finds the one person a control socket client means by `ref` when no chat
 * matched it: a phone number or personal JID names them outright, and
 * otherwise `ref` is a name looked for among `contacts` (the whole name in
 * any case, then the start of a word in it). Only people are found, never a
 * group, and only those the automation policy would allow as a chat. Someone
 * who already has a chat is not found here: if their chat did not match, it
 * is one agents may not use. On FOUND, *found is the person; on AMBIGUOUS,
 * the people who matched go to `candidates` (up to `max`). */
ChatResolution recipient_resolve(const Settings *settings, const Chat *chats, int chat_count,
                                 const Contact *contacts, int contact_count, const char *ref,
                                 Recipient *found, Recipient *candidates, int max, int *candidate_count);

#endif
