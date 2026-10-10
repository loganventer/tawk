#ifndef APP_CLIENTS_ACCOUNT_SERVICES_H
#define APP_CLIENTS_ACCOUNT_SERVICES_H

#include "core/account_id.h"
#include "managers/account_manager.h"
#include "managers/call_manager.h"
#include "managers/messaging_manager.h"
#include "managers/profile_manager.h"
#include "managers/scheduling_manager.h"
#include "managers/status_feed_manager.h"
#include "managers/status_manager.h"
#include "managers/summary_manager.h"
#include "managers/transcript_manager.h"

/* The managers that work for one account. Managers never call each other,
 * so a client that needs two of them, or the same one for several accounts,
 * is handed them like this. */
typedef struct AccountServices {
    AccountId          id;
    const char        *label;          /* the account's label, as it is now */
    const char        *backend_name;
    MessagingManager  *messaging;
    ProfileManager    *profiles;
    CallManager       *calls;
    AccountManager    *accounts;
    StatusManager     *statuses;
    StatusFeedManager *feed;
    SchedulingManager *scheduling;
    TranscriptManager *transcripts;
    SummaryManager    *summaries;
} AccountServices;

#endif
