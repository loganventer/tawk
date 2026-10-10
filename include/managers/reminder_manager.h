#ifndef APP_MANAGERS_REMINDER_MANAGER_H
#define APP_MANAGERS_REMINDER_MANAGER_H

#include <stdint.h>

#include "contracts/i_reminder_store.h"

/* Chats put aside until a time, or until their person writes. A chat with a
 * reminder is left out of the list until it comes back. */
typedef struct ReminderManager ReminderManager;

ReminderManager *reminder_manager_create(IReminderStore *store);
void             reminder_manager_destroy(ReminderManager *mgr);

/* Puts `jid` aside until `due_at` (0: until they write). */
int  reminder_manager_set(ReminderManager *mgr, const char *jid, int64_t due_at, int64_t now);
int  reminder_manager_clear(ReminderManager *mgr, const char *jid);
/* Whether `jid` is put aside, and until when (0: until they write). */
int  reminder_manager_snoozed(ReminderManager *mgr, const char *jid, int64_t *due_at);
int  reminder_manager_count(ReminderManager *mgr);
/* The chat put aside at `index`, or NULL. */
const ChatReminder *reminder_manager_at(ReminderManager *mgr, int index);
/* A chat whose time has come, or that has unread messages (`unread` says how
 * many `jid` has): its reminder is removed and 1 is returned. */
int  reminder_manager_take_due(ReminderManager *mgr, const char *jid, int unread, int64_t now);

#endif
