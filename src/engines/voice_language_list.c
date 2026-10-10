#include "engines/voice_language_list.h"
#include "core/voice_language.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

int voice_language_list_clean(const char *list, char *out, size_t size) {
    char copy[256];
    int kept = 0;
    size_t used = 0;
    out[0] = '\0';
    str_copy(copy, sizeof(copy), list ? list : "");
    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        const VoiceLanguage *known = voice_language_find(str_trim(tok));
        if (!known) continue;
        char probe[16];
        snprintf(probe, sizeof(probe), ",%s,", known->code);
        char have[300];
        snprintf(have, sizeof(have), ",%s,", out);
        if (strstr(have, probe)) continue;                  /* already in */
        if (used + strlen(known->code) + 2 >= size) break;
        used += (size_t)snprintf(out + used, size - used, "%s%s", used ? "," : "", known->code);
        kept++;
    }
    return kept;
}
