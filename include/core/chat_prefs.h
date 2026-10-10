#ifndef APP_CORE_CHAT_PREFS_H
#define APP_CORE_CHAT_PREFS_H

#include "core/account_id.h"
#include "core/chat_merge_choice.h"
#include "core/chat_transcript_choice.h"

/* What you chose for a person or group, whichever of your accounts they are on. */
typedef struct ChatPrefs {
    char            jid[128];
    AccountId       send_account;   /* ACCOUNT_ID_NONE: the primary account */
    ChatMergeChoice merge;
    ChatTranscriptChoice show_transcripts;  /* whether the transcripts of its voice notes show */
    int             transcribe_off;         /* 1: nothing in this chat is transcribed from now on */
    int             tldr;                   /* 1: this chat's messages show as a summary */
    char            voice_languages[64];    /* the languages its voice notes are spoken in ("af,en"); "": any, as the setting says */
} ChatPrefs;

#endif
