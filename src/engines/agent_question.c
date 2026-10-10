#include "engines/agent_question.h"
#include "utilities/str_util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void agent_question_text(const AutomationSession *agents, int count, char *out, size_t size) {
    size_t used = (size_t)snprintf(out, size, "tawk: which agent should write TL;DR summaries? Answer with its number.\n");
    for (int i = 0; i < count && used < size; i++) {
        const AutomationSession *a = &agents[i];
        used += (size_t)snprintf(out + used, size - used, "\n%d. %s%s%s%s%s", i + 1, a->client,
                                 a->label[0] ? ", " : "", a->label, a->doing[0] ? ": " : "", a->doing);
    }
}

int agent_question_answer(const char *text, int count) {
    if (!text) return -1;
    while (isspace((unsigned char)*text)) text++;
    if (!isdigit((unsigned char)*text)) return -1;
    int n = 0, digits = 0;
    while (isdigit((unsigned char)*text) && digits < 3) { n = n * 10 + (*text - '0'); text++; digits++; }
    if (*text == '.') text++;
    while (isspace((unsigned char)*text)) text++;
    return *text == '\0' && n >= 1 && n <= count ? n - 1 : -1;
}

void agent_question_label_key(const char *label, char *out, size_t size) {
    str_copy(out, size, label ? label : "");
    char *pid = strstr(out, ", pid ");
    if (!pid) pid = strstr(out, " pid ");
    if (!pid) return;
    char *end = pid + (pid[0] == ',' ? 6 : 5);
    while (isdigit((unsigned char)*end)) end++;
    memmove(pid, end, strlen(end) + 1);
}
