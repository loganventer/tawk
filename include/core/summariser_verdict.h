#ifndef APP_CORE_SUMMARISER_VERDICT_H
#define APP_CORE_SUMMARISER_VERDICT_H

/* Who writes a TL;DR summary now. */
typedef enum SummariserVerdict {
    SUMMARISER_NONE = 0,       /* no agent is connected */
    SUMMARISER_USE,            /* this one */
    SUMMARISER_ASK             /* several are connected and none is chosen: you are asked */
} SummariserVerdict;

#endif
