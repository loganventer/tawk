#ifndef APP_ENGINES_TRANSCRIPT_DISPLAY_POLICY_H
#define APP_ENGINES_TRANSCRIPT_DISPLAY_POLICY_H

#include "core/chat_transcript_choice.h"

/* Whether a chat shows the transcripts of its voice notes: what was chosen
 * for the chat, or the setting when nothing was. */
int transcript_display_policy_shows(int setting_on, ChatTranscriptChoice choice);
/* The choice that follows `choice` when you step through them. */
ChatTranscriptChoice transcript_display_policy_next(ChatTranscriptChoice choice);

#endif
