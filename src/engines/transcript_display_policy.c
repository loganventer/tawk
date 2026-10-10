#include "engines/transcript_display_policy.h"

int transcript_display_policy_shows(int setting_on, ChatTranscriptChoice choice) {
    if (choice == CHAT_TRANSCRIPT_ALWAYS) return 1;
    if (choice == CHAT_TRANSCRIPT_NEVER) return 0;
    return setting_on != 0;
}

ChatTranscriptChoice transcript_display_policy_next(ChatTranscriptChoice choice) {
    if (choice == CHAT_TRANSCRIPT_FOLLOW) return CHAT_TRANSCRIPT_ALWAYS;
    if (choice == CHAT_TRANSCRIPT_ALWAYS) return CHAT_TRANSCRIPT_NEVER;
    return CHAT_TRANSCRIPT_FOLLOW;
}
