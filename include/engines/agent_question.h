#ifndef APP_ENGINES_AGENT_QUESTION_H
#define APP_ENGINES_AGENT_QUESTION_H

#include <stddef.h>

#include "core/automation_session.h"

/* The question tawk puts to you when several agents are connected and none
 * is chosen to write TL;DR summaries: a numbered list to answer with a number. */
void agent_question_text(const AutomationSession *agents, int count, char *out, size_t size);
/* The agent an answer names, from 0, or -1 when the text is not a number of
 * the list and nothing else ("2", "2." and " 2 " are; "2 please" is not). */
int  agent_question_answer(const char *text, int count);
/* What stays the same about an agent's label when it connects again: the
 * label without its process id ("wats (stdio, pid 3002)" gives "wats (stdio)"). */
void agent_question_label_key(const char *label, char *out, size_t size);

#endif
