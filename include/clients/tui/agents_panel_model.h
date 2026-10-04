#ifndef APP_CLIENTS_TUI_AGENTS_PANEL_MODEL_H
#define APP_CLIENTS_TUI_AGENTS_PANEL_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "clients/tui/approval_queue.h"
#include "core/automation_entry.h"
#include "core/automation_status.h"
#include "core/settings.h"

/* What the Agents tab shows, gathered by its owner for each key and frame. */
typedef struct AgentsPanelModel {
    const ApprovalQueue    *queue;
    const AutomationStatus *status;
    const AutomationEntry  *log;          /* newest first */
    int                     log_count;
    const Settings         *settings;
    int64_t                 now_ms;       /* monotonic, as the requests' times */
    /* The name of a chat in one of your accounts, and that account's label ("" with one account). */
    void                  (*name_of)(void *ctx, AccountId account, const char *jid, char *out, size_t size);
    void                  (*label_of)(void *ctx, AccountId account, char *out, size_t size);
    void                   *ctx;
    const char             *self_chats;   /* how many chats an admin agent answers for itself, in words */
} AgentsPanelModel;

#endif
