#ifndef APP_CLIENTS_CONTROL_CONTROL_SERVER_DEPS_H
#define APP_CLIENTS_CONTROL_CONTROL_SERVER_DEPS_H

#include "clients/i_account_directory.h"
#include "contracts/i_approval_prompt.h"
#include "managers/account_roster_manager.h"
#include "contracts/i_control_transport.h"
#include "managers/account_manager.h"
#include "managers/automation_manager.h"
#include "managers/call_manager.h"
#include "managers/messaging_manager.h"
#include "managers/profile_manager.h"
#include "managers/scheduling_manager.h"
#include "managers/settings_manager.h"
#include "managers/status_feed_manager.h"
#include "managers/status_manager.h"
#include "managers/summary_manager.h"
#include "managers/transcript_manager.h"

/* Everything the control client uses, injected by the composition root. */
typedef struct ControlServerDeps {
    IControlTransport  *transport;
    IApprovalPrompt    *approvals;
    MessagingManager   *messaging;
    ProfileManager     *profiles;
    SchedulingManager  *scheduling;
    StatusFeedManager  *feed;
    AutomationManager  *automation;
    SettingsManager    *settings;
    AccountManager     *accounts;         /* your profile */
    StatusManager      *statuses;         /* posting statuses */
    CallManager        *calls;
    const char         *backend_name;
    const char         *socket_path;
    /* The managers above are those of the account being served. With a
     * directory and a roster, each request is served by the account it names;
     * without them there is the one account, as there always was. */
    IAccountDirectory  *directory;
    AccountRosterManager *roster;
    TranscriptManager  *transcripts;      /* voice note transcripts; NULL when this tawk keeps none */
    SummaryManager     *summaries;        /* TL;DR summaries; NULL when this tawk keeps none */
} ControlServerDeps;

#endif
