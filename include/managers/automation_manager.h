#ifndef APP_MANAGERS_AUTOMATION_MANAGER_H
#define APP_MANAGERS_AUTOMATION_MANAGER_H

#include <stdint.h>

#include "core/account_id.h"
#include "core/approval_risk.h"
#include "core/automation_command.h"
#include "core/automation_entry.h"
#include "core/automation_status.h"
#include "core/automation_verdict.h"
#include "core/chat.h"
#include "core/chat_agent_rule.h"
#include "core/chat_resolution.h"
#include "core/contact.h"
#include "core/control_origin.h"
#include "core/live_kind.h"
#include "core/recipient.h"
#include "core/self_approval_verdict.h"
#include "core/setting_field.h"
#include "core/write_kind.h"
#include "managers/automation_manager_deps.h"

/* The rules for programs that reach tawk through the control socket: which
 * chats they see, whether a write may go ahead now, after you allow it, or
 * not at all, how many writes a minute they get, and the log of what they
 * did. It does nothing itself: the control client carries out what it
 * allows, and reports what happened. */
typedef struct AutomationManager AutomationManager;

AutomationManager *automation_manager_create(const AutomationManagerDeps *deps);
void               automation_manager_destroy(AutomationManager *mgr);

/* Serves one of your accounts from here on: its access level ("read",
 * "send", "manage" or "admin") and the chats an agent may answer by itself
 * in take the place of what the settings say, and what is logged names it.
 * A NULL access goes back to the settings, as with one account. */
void automation_manager_serve(AutomationManager *mgr, AccountId account, const char *access, const char *self_chats);
/* Whether any account's access is admin, so the admin token is kept for it.
 * Until this is called the access setting alone decides. */
void automation_manager_admin_wanted(AutomationManager *mgr, int wanted);

int               automation_manager_chat_allowed(AutomationManager *mgr, const Chat *chat);
/* The rule you gave one chat for agents, and setting it. A rule only tightens what the account allows. */
ChatAgentRule     automation_manager_chat_rule(AutomationManager *mgr, const char *jid);
int               automation_manager_set_chat_rule(AutomationManager *mgr, const char *jid, ChatAgentRule rule);
/* Whether what a client of that origin reads has its codes and card numbers hidden. */
int               automation_manager_masks(AutomationManager *mgr, ControlOrigin origin);
/* The one chat `ref` names among `chats`; see chat_reference_resolve. */
ChatResolution    automation_manager_resolve(AutomationManager *mgr, const Chat *chats, int count, const char *ref,
                                             int *found, int *candidates, int max, int *candidate_count);
/* The one person `ref` names who has no chat yet, among `contacts`; see recipient_resolve. */
ChatResolution    automation_manager_resolve_recipient(AutomationManager *mgr, const Chat *chats, int chat_count,
                                                       const Contact *contacts, int contact_count, const char *ref,
                                                       Recipient *found, Recipient *candidates, int max, int *candidate_count);
/* Whether a write may go ahead; each allowed or asked write counts against
 * the rate. *retry_after_s is set when RATE_LIMITED. */
AutomationVerdict automation_manager_check_write(AutomationManager *mgr, ControlOrigin origin, WriteKind kind,
                                                int64_t now_ms, int *retry_after_s);
/* "read", "send", "manage" or "admin", as the settings say. */
const char       *automation_manager_access(AutomationManager *mgr);
/* Whether a subscribed client of that origin is told about a new message (yours when `from_me`) as it arrives. */
int               automation_manager_pushes(AutomationManager *mgr, ControlOrigin origin, int from_me);
/* Whether a subscribed client of that origin is told about a read, a reaction, an edit or delete, or a scheduled send. */
int               automation_manager_pushes_event(AutomationManager *mgr, ControlOrigin origin, LiveKind kind);
int               automation_manager_presence_lookup(AutomationManager *mgr, ControlOrigin origin);
/* The disclaimer to add under a message of this operation from this origin, or NULL for none. */
const char       *automation_manager_disclaimer(AutomationManager *mgr, ControlOrigin origin, const char *op);
int               automation_manager_setting_changeable(AutomationManager *mgr, const SettingField *field);

/* How risky a write is: destructive ones HIGH, reactions and read marks LOW, the rest MEDIUM. */
ApprovalRisk automation_manager_risk(AutomationManager *mgr, const char *op, WriteKind kind);
/* How long a request of that risk waits for you before it is declined. */
int64_t      automation_manager_answer_window_ms(AutomationManager *mgr, ApprovalRisk risk);

/* Keeps the admin token in step with the settings: a fresh one is written
 * when access becomes admin (and each time tawk starts), and it is removed
 * when access is anything else. Call it regularly. */
void automation_manager_tick(AutomationManager *mgr);
/* Whether a client may answer its own waiting request for `op` in `chat`
 * (NULL: about no chat): the settings, the admin token it showed, and this
 * hour's allowance, which an allowed one counts against. *retry_after_s is
 * set when RATE_LIMITED. */
SelfApprovalVerdict automation_manager_self_approve(AutomationManager *mgr, const char *op, const Chat *chat,
                                                    const char *token, int64_t now_ms, int *retry_after_s);
/* A line for you about something a client did by itself, and the next one waiting (1 when there was one). */
void automation_manager_notice(AutomationManager *mgr, const char *text);
int  automation_manager_take_notice(AutomationManager *mgr, char *out, unsigned long size);

/* Things you ask for in the Agents tab, handed to the control client. */
void automation_manager_command(AutomationManager *mgr, AutomationCommandKind kind, int conn);
int  automation_manager_take_command(AutomationManager *mgr, AutomationCommand *out);

/* A fresh one-time token for a destructive request waiting to be confirmed. */
int  automation_manager_new_token(AutomationManager *mgr, char *out, unsigned long size);

void automation_manager_record(AutomationManager *mgr, ControlOrigin origin, const char *client, const char *op,
                               const char *chat_jid, const char *summary, AutomationOutcome outcome);
/* Newest first; the caller frees `*out`. */
int  automation_manager_recent(AutomationManager *mgr, int limit, AutomationEntry **out, int *count);

/* What the control socket is doing, reported by the control client and read by the screen. */
void                    automation_manager_set_status(AutomationManager *mgr, const AutomationStatus *status);
const AutomationStatus *automation_manager_status(AutomationManager *mgr);
/* True once after the status or the log changed. */
int                     automation_manager_take_changed(AutomationManager *mgr);

#endif
