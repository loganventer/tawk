#include "clients/cli/control_options.h"

#include <stdio.h>
#include <string.h>

ControlCommandKind control_command_kind_of(const char *word) {
    if (!word) return CONTROL_COMMAND_NONE;
    if (!strcmp(word, "send")) return CONTROL_COMMAND_SEND;
    if (!strcmp(word, "tail")) return CONTROL_COMMAND_TAIL;
    if (!strcmp(word, "unread")) return CONTROL_COMMAND_UNREAD;
    if (!strcmp(word, "status-line")) return CONTROL_COMMAND_STATUS_LINE;
    return CONTROL_COMMAND_NONE;
}

int control_options_parse(ControlCommandKind kind, int argc, char **argv, ControlOptions *o) {
    memset(o, 0, sizeof(*o));
    o->kind = kind;
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--json")) { o->json = 1; continue; }
        if (!strcmp(a, "--format") && i + 1 < argc) { o->format = argv[++i]; continue; }
        if (!strcmp(a, "--account") && i + 1 < argc) { o->account = argv[++i]; continue; }
        if (kind == CONTROL_COMMAND_SEND) {
            if (o->chat_count == 0) { o->chats[o->chat_count++] = a; continue; }
            if (!strcmp(a, "-") && o->word_count == 0) { o->from_stdin = 1; continue; }
            if (o->word_count < 64) o->words[o->word_count++] = a;
            continue;
        }
        if (kind == CONTROL_COMMAND_TAIL && o->chat_count < CONTROL_OPTIONS_CHATS && a[0] != '-') { o->chats[o->chat_count++] = a; continue; }
        fprintf(stderr, "tawk: unexpected %s\n", a);
        return -1;
    }
    if (kind == CONTROL_COMMAND_SEND && (o->chat_count == 0 || (o->word_count == 0 && !o->from_stdin))) {
        fprintf(stderr, "usage: tawk send CHAT TEXT...   or   tawk send CHAT - < file\n");
        return -1;
    }
    return 0;
}
