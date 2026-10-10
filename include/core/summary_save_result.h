#ifndef APP_CORE_SUMMARY_SAVE_RESULT_H
#define APP_CORE_SUMMARY_SAVE_RESULT_H

/* What became of a summary handed over for a message. */
typedef enum SummarySaveResult {
    SUMMARY_SAVED = 0,
    SUMMARY_OFF,               /* this chat is not in TL;DR mode */
    SUMMARY_REFUSED,           /* not a text message, or the summary is not acceptable */
    SUMMARY_FAILED             /* it could not be stored */
} SummarySaveResult;

#endif
