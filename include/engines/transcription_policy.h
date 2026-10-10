#ifndef APP_ENGINES_TRANSCRIPTION_POLICY_H
#define APP_ENGINES_TRANSCRIPTION_POLICY_H

#include "core/chat.h"
#include "core/chat_prefs.h"

/* Whether a new transcript may be made or kept for a chat: not when you
 * switched transcribing off for it, and never for a locked or soft-locked
 * chat. It looks forwards only: transcripts a chat already has are not its
 * business. */
int transcription_policy_allows(const Chat *chat, const ChatPrefs *prefs);

#endif
