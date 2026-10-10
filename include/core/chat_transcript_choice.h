#ifndef APP_CORE_CHAT_TRANSCRIPT_CHOICE_H
#define APP_CORE_CHAT_TRANSCRIPT_CHOICE_H

/* Whether one chat shows the transcripts of its voice notes. */
typedef enum ChatTranscriptChoice {
    CHAT_TRANSCRIPT_FOLLOW = 0,     /* as the setting says */
    CHAT_TRANSCRIPT_ALWAYS,
    CHAT_TRANSCRIPT_NEVER
} ChatTranscriptChoice;

#endif
