#ifndef APP_CORE_TRANSCRIPT_SAVE_RESULT_H
#define APP_CORE_TRANSCRIPT_SAVE_RESULT_H

/* What became of a transcript handed over for a message. */
typedef enum TranscriptSaveResult {
    TRANSCRIPT_SAVED = 0,
    TRANSCRIPT_OFF,            /* this chat is not transcribed */
    TRANSCRIPT_REFUSED,        /* not a voice note, or the text or language is not acceptable */
    TRANSCRIPT_FAILED          /* it could not be stored */
} TranscriptSaveResult;

#endif
