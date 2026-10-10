#ifndef APP_MANAGERS_SUMMARY_MANAGER_H
#define APP_MANAGERS_SUMMARY_MANAGER_H

#include <stddef.h>

#include "core/chat.h"
#include "core/message.h"
#include "core/summary.h"
#include "core/summary_save_result.h"
#include "managers/summary_manager_deps.h"

/* TL;DR for one account: which chats are in that mode, the summaries an
 * agent hands over, and the list of long messages still waiting for one.
 * It writes no summary itself. */
typedef struct SummaryManager SummaryManager;

SummaryManager *summary_manager_create(const SummaryManagerDeps *deps);
void            summary_manager_destroy(SummaryManager *mgr);
/* Why the last save or choice was refused. */
const char     *summary_manager_error(SummaryManager *mgr);

/* Whether a chat is in TL;DR mode, and switching it. Switching it off keeps
 * the summaries it has; they are simply not shown. */
int  summary_manager_tldr(SummaryManager *mgr, const char *chat_jid);
int  summary_manager_set_tldr(SummaryManager *mgr, const char *chat_jid, int on);

/* Keeps the summary of `message`, a text message in `chat`. `source` names the program that hands it over. */
SummarySaveResult summary_manager_save(SummaryManager *mgr, const Message *message, const Chat *chat,
                                       const char *text, const char *model, const char *source);
/* Fills `out` (the caller disposes it) and returns 0, or -1 when the message has none. */
int  summary_manager_find(SummaryManager *mgr, const char *message_id, Summary *out);

/* Whether `message` in `chat` is one to summarise: the chat is in TL;DR mode and the message is long enough. */
int  summary_manager_wants(SummaryManager *mgr, const Message *message, const Chat *chat);
/* Puts a message on the list of those waiting for a summary, once: one that
 * has a summary, is already waiting or was asked for a while ago is left out. */
void summary_manager_want(SummaryManager *mgr, const Message *message, const Chat *chat);
/* Puts the older long messages of a TL;DR chat on the waiting list, newest
 * first: those of the last `tldr_back_days` days, from other people, that
 * have no summary. Done once for a chat while tawk runs, and again after its
 * TL;DR is switched on. Returns how many were added. */
int  summary_manager_backfill(SummaryManager *mgr, const Chat *chat, int64_t now);
/* Puts a message back at the head of the list: the agent that was asked for
 * its summary did not answer, so another is asked. */
void summary_manager_requeue(SummaryManager *mgr, const char *message_id);
/* The message that has waited longest (returns 0 when none waits), and taking it off the list. */
int  summary_manager_next_wanted(SummaryManager *mgr, char *message_id, size_t size);
void summary_manager_drop_wanted(SummaryManager *mgr);

/* True once after a summary was kept or a chat's mode changed, so the screen can be drawn again. */
int  summary_manager_take_changed(SummaryManager *mgr);

#endif
