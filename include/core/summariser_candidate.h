#ifndef APP_CORE_SUMMARISER_CANDIDATE_H
#define APP_CORE_SUMMARISER_CANDIDATE_H

/* A connected agent that could write TL;DR summaries. */
typedef struct SummariserCandidate {
    int conn;
    int chosen;       /* you picked this one */
} SummariserCandidate;

#endif
