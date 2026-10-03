#ifndef APP_CLIENTS_CLI_CONTROL_OPTIONS_H
#define APP_CLIENTS_CLI_CONTROL_OPTIONS_H

#include "clients/cli/control_command_kind.h"

#define CONTROL_OPTIONS_CHATS 32

/* What a shell command asked for: tawk send CHAT [TEXT|-], tawk tail
 * [CHAT...] [--json], tawk unread [--json], tawk status-line [--format F],
 * each with --account NAME to pick one of your accounts. */
typedef struct ControlOptions {
    ControlCommandKind kind;
    const char        *chats[CONTROL_OPTIONS_CHATS];
    int                chat_count;
    const char        *words[64];          /* send: the text, as separate arguments */
    int                word_count;
    int                from_stdin;         /* send: "-" reads the text from standard input */
    int                json;
    const char        *format;             /* status-line */
    const char        *account;            /* --account: its label or id; NULL for the default, or all when counting */
} ControlOptions;

/* Reads the arguments after the command name; returns 0, or -1 with a
 * message on standard error. */
int control_options_parse(ControlCommandKind kind, int argc, char **argv, ControlOptions *out);
/* The command a word names ("send", "tail", "unread", "status-line"), or NONE. */
ControlCommandKind control_command_kind_of(const char *word);

#endif
