#include "engines/summariser_choice.h"

SummariserVerdict summariser_choice_pick(const SummariserCandidate *candidates, int count, int *conn) {
    if (!candidates || count <= 0) return SUMMARISER_NONE;
    for (int i = 0; i < count; i++) {
        if (candidates[i].chosen) { *conn = candidates[i].conn; return SUMMARISER_USE; }
    }
    if (count == 1) { *conn = candidates[0].conn; return SUMMARISER_USE; }
    return SUMMARISER_ASK;
}
