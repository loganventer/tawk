#ifndef APP_ENGINES_SUMMARISER_CHOICE_H
#define APP_ENGINES_SUMMARISER_CHOICE_H

#include "core/summariser_candidate.h"
#include "core/summariser_verdict.h"

/* Which connected agent writes a TL;DR summary: the one you chose when it
 * is connected; else the only one connected; else, with several and none
 * chosen, you are asked. *conn receives the agent for SUMMARISER_USE. */
SummariserVerdict summariser_choice_pick(const SummariserCandidate *candidates, int count, int *conn);

#endif
