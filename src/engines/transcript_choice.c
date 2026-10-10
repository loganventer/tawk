#include "engines/transcript_choice.h"
#include "utilities/str_util.h"

#include <string.h>
#include <strings.h>

int transcript_choice_pick(const Transcript *items, int count, const char *languages) {
    if (!items || count <= 0) return -1;
    char list[96];
    str_copy(list, sizeof(list), languages ? languages : "");
    char *save = NULL;
    for (char *tok = strtok_r(list, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        const char *want = str_trim(tok);
        if (!want[0] || strcasecmp(want, "auto") == 0) continue;
        for (int i = 0; i < count; i++) if (strcasecmp(items[i].language, want) == 0) return i;
    }
    return 0;
}
