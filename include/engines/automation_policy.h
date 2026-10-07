#ifndef APP_ENGINES_AUTOMATION_POLICY_H
#define APP_ENGINES_AUTOMATION_POLICY_H

#include "core/approval_risk.h"
#include "core/automation_verdict.h"
#include "core/chat.h"
#include "core/control_origin.h"
#include "core/live_kind.h"
#include "core/self_approval_verdict.h"
#include "core/setting_field.h"
#include "core/settings.h"
#include "core/write_kind.h"

/* What control socket clients may see and do, from the Automation settings.
 * Locked chats, soft-locked chats and broadcast feeds are never shown; a
 * non-empty chat list narrows the rest to the chats it names (by name,
 * JID or phone number). */
int               automation_policy_chat_allowed(const Settings *settings, const Chat *chat);
/* Sending needs access "send", anything else "manage". Clients acting for
 * a model always ask you first; your own shell commands ask when
 * confirm_cli is on, and always for destructive writes. */
AutomationVerdict automation_policy_write(const Settings *settings, ControlOrigin origin, WriteKind kind);
/* Whether a client may answer its own waiting request, by the settings
 * alone (the admin token and the hourly allowance are the manager's): access
 * must be admin, the request a send or a small thing (a message, a reply, a
 * forward, an edit, a retry, anything scheduled, a reaction, a read mark, a
 * like), and its chat one the client may use at all and one you chose for
 * this (the self-approval chats). With none chosen nothing is allowed:
 * answering for you is only for chats you picked. `chat` may be NULL for a
 * request about no chat, which is never allowed. */
SelfApprovalVerdict automation_policy_self_approval(const Settings *settings, const char *op, const Chat *chat);
/* Whether a client that subscribed is told about a new message as it
 * arrives: programs acting for a model follow the two push settings (one
 * for what other people send, one for what you send); your own shell
 * commands, such as tawk tail, always are. */
int               automation_policy_pushes(const Settings *settings, ControlOrigin origin, int from_me);
/* Whether a client that subscribed is told about something other than a new
 * message (a read, a reaction, an edit or delete, a scheduled send): only
 * programs acting for a model, and only with that kind's setting on. */
int               automation_policy_pushes_event(const Settings *settings, ControlOrigin origin, LiveKind kind);
/* Whether a client may ask if someone is online. Your own shell always may; an agent only when you switched it on. */
int               automation_policy_presence_lookup(const Settings *settings, ControlOrigin origin);
/* The line to add under a message of this operation from this origin, or
 * NULL for none: only what a program acting for a model sends, schedules or
 * answers a status with, and only with the disclaimer turned on. */
const char       *automation_policy_disclaimer(const Settings *settings, ControlOrigin origin, const char *op);
/* Whether a setting may be changed over the control socket: never the
 * automation settings, commands, folders, the backend or the log level. */
int               automation_policy_setting_changeable(const SettingField *field);
/* Destructive writes are HIGH, reactions and read marks LOW, everything else MEDIUM. */
ApprovalRisk      automation_policy_risk(const char *op, WriteKind kind);

#endif
