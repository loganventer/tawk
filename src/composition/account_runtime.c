#include "composition/account_runtime.h"
#include "infrastructure/ifaddrs_network_monitor.h"
#include "managers/composite_event_observer.h"
#include "resource_access/caching_contact_store.h"
#include "resource_access/caching_message_store.h"
#include "resource_access/sqlite_chat_store.h"
#include "resource_access/sqlite_contact_store.h"
#include "resource_access/sqlite_jid_alias_store.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_profile_store.h"
#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_receipt_store.h"
#include "resource_access/sqlite_scheduled_message_store.h"
#include "resource_access/sqlite_status_store.h"
#include "resource_access/sqlite_chat_prefs_store.h"
#include "resource_access/sqlite_summary_store.h"
#include "resource_access/sqlite_transcript_store.h"
#include "utilities/event_queue.h"
#include "utilities/log.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_QUEUE_SIZE   1024
#define CACHED_CHATS       16
#define CACHED_CONTACTS    512

struct AccountRuntime {
    AccountServices services;
    char            label[ACCOUNT_LABEL_SIZE];

    /* Storage: this account's rows of the shared database, behind caches of its own */
    IMessageStore          *messages;
    IChatStore             *chats;
    IContactStore          *contacts;
    IJidAliasStore         *aliases;
    IReactionStore         *reactions;
    IReceiptStore          *receipts;
    IProfileStore          *profile_store;
    IStatusStore           *status_store;
    IScheduledMessageStore *scheduled_store;
    ITranscriptStore       *transcript_store;
    ISummaryStore          *summary_store;

    /* Backend */
    EventQueue      *events;
    IMessageGateway *gateway;
    INetworkMonitor *network;
    IEventObserver  *observers;
};

void account_runtime_auth_dir(const Settings *settings, AccountId id, char *out, size_t size) {
    if (id <= ACCOUNT_ID_FIRST) {
        settings_auth_dir(settings, out, size);
        return;
    }
    char accounts[600], own[700], number[16];
    path_join(accounts, sizeof(accounts), settings->data_dir, "accounts");
    snprintf(number, sizeof(number), "%d", id);
    path_join(own, sizeof(own), accounts, number);
    path_join(out, size, own, "auth");
}

AccountRuntime *account_runtime_create(const AccountRuntimeParams *p, const Account *account) {
    AccountRuntime *rt = calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    const AccountId id = account->id;
    const Settings *s = p->settings;
    str_copy(rt->label, sizeof(rt->label), account->label);
    rt->services.id = id;
    rt->services.label = rt->label;

    rt->messages = caching_message_store_create(sqlite_message_store_create(p->db, id), CACHED_CHATS);
    rt->chats = sqlite_chat_store_create(p->db, id);
    rt->contacts = caching_contact_store_create(sqlite_contact_store_create(p->db, id), CACHED_CONTACTS);
    rt->aliases = sqlite_jid_alias_store_create(p->db, id);
    rt->reactions = sqlite_reaction_store_create(p->db, id);
    rt->receipts = sqlite_receipt_store_create(p->db, id);
    rt->profile_store = sqlite_profile_store_create(p->db, id);
    rt->status_store = sqlite_status_store_create(p->db, id);
    rt->scheduled_store = sqlite_scheduled_message_store_create(p->db, id);
    rt->transcript_store = sqlite_transcript_store_create(p->db, id);
    rt->summary_store = sqlite_summary_store_create(p->db, id);

    rt->events = event_queue_create(EVENT_QUEUE_SIZE);
    GatewayParts parts;
    memset(&parts, 0, sizeof(parts));
    if (rt->events && p->gateways->create(p->gateways, id, rt->events, &parts) != 0) parts.gateway = NULL;
    rt->gateway = parts.gateway;
    rt->services.backend_name = parts.backend_name ? parts.backend_name : "";
    IProfileEditor *editor = parts.editor;
    IStatusPublisher *publisher = parts.publisher;
    IStatusLiker *liker = parts.liker;
    if (!rt->messages || !rt->chats || !rt->contacts || !rt->aliases || !rt->reactions || !rt->receipts || !rt->profile_store ||
        !rt->status_store || !rt->scheduled_store || !rt->transcript_store || !rt->summary_store || !rt->gateway) {
        LOG_ERROR("account %d could not be started", id);
        account_runtime_destroy(rt);
        return NULL;
    }

    ProfileManagerDeps profile_deps = { rt->gateway, rt->profile_store, s->media_dir };
    rt->services.profiles = profile_manager_create(&profile_deps);
    rt->services.calls = call_manager_create(rt->gateway);
    AccountManagerDeps account_deps = { editor, s->media_dir };
    rt->services.accounts = account_manager_create(&account_deps);
    StatusManagerDeps status_deps = { publisher, s->media_dir };
    rt->services.statuses = status_manager_create(&status_deps);
    StatusFeedManagerDeps feed_deps = { rt->status_store, rt->gateway, s->media_dir, s, rt->receipts, rt->reactions };
    rt->services.feed = status_feed_manager_create(&feed_deps);
    SchedulingManagerDeps scheduling_deps = { rt->scheduled_store };
    rt->services.scheduling = scheduling_manager_create(&scheduling_deps);
    TranscriptManagerDeps transcript_deps = { rt->transcript_store, p->chat_prefs, p->transcript_prefs, s };
    rt->services.transcripts = transcript_manager_create(&transcript_deps);
    SummaryManagerDeps summary_deps = { rt->summary_store, p->chat_prefs, p->summary_prefs, s, rt->messages };
    rt->services.summaries = summary_manager_create(&summary_deps);

    rt->observers = composite_event_observer_create();
    composite_event_observer_add(rt->observers, profile_manager_observer(rt->services.profiles));
    composite_event_observer_add(rt->observers, call_manager_observer(rt->services.calls));
    composite_event_observer_add(rt->observers, account_manager_observer(rt->services.accounts));
    composite_event_observer_add(rt->observers, status_manager_observer(rt->services.statuses));
    composite_event_observer_add(rt->observers, status_feed_manager_observer(rt->services.feed));
    composite_event_observer_add(rt->observers, scheduling_manager_observer(rt->services.scheduling));

    /* Each account watches the network for itself: noticing a change uses it up. */
    rt->network = ifaddrs_network_monitor_create();
    MessagingManagerDeps messaging_deps = { rt->gateway, rt->messages, rt->chats, rt->contacts, rt->aliases, rt->reactions, rt->receipts,
                                            p->notifier, rt->events, s, rt->observers, p->exporter, rt->network, liker,
                                            id, rt->label, p->chat_prefs, sqlite_chat_prefs_store_alerts(p->chat_prefs) };
    rt->services.messaging = messaging_manager_create(&messaging_deps);
    if (!rt->services.messaging) {
        account_runtime_destroy(rt);
        return NULL;
    }
    return rt;
}

void account_runtime_destroy(AccountRuntime *rt) {
    if (!rt) return;
    if (rt->services.messaging) messaging_manager_destroy(rt->services.messaging);
    if (rt->network) rt->network->destroy(rt->network);
    if (rt->services.profiles) profile_manager_destroy(rt->services.profiles);
    if (rt->services.calls) call_manager_destroy(rt->services.calls);
    if (rt->observers) rt->observers->destroy(rt->observers);
    if (rt->services.feed) status_feed_manager_destroy(rt->services.feed);
    if (rt->services.scheduling) scheduling_manager_destroy(rt->services.scheduling);
    if (rt->services.transcripts) transcript_manager_destroy(rt->services.transcripts);
    if (rt->services.summaries) summary_manager_destroy(rt->services.summaries);
    if (rt->services.statuses) status_manager_destroy(rt->services.statuses);
    if (rt->services.accounts) account_manager_destroy(rt->services.accounts);
    /* Closing the queue first unblocks a backend that is waiting on
     * back-pressure, so it can shut down. */
    if (rt->events) event_queue_close(rt->events);
    if (rt->gateway) rt->gateway->destroy(rt->gateway);
    if (rt->events) event_queue_destroy(rt->events);
    if (rt->reactions) rt->reactions->destroy(rt->reactions);
    if (rt->receipts) rt->receipts->destroy(rt->receipts);
    if (rt->profile_store) rt->profile_store->destroy(rt->profile_store);
    if (rt->status_store) rt->status_store->destroy(rt->status_store);
    if (rt->scheduled_store) rt->scheduled_store->destroy(rt->scheduled_store);
    if (rt->transcript_store) rt->transcript_store->destroy(rt->transcript_store);
    if (rt->summary_store) rt->summary_store->destroy(rt->summary_store);
    if (rt->aliases) rt->aliases->destroy(rt->aliases);
    if (rt->contacts) rt->contacts->destroy(rt->contacts);
    if (rt->chats) rt->chats->destroy(rt->chats);
    if (rt->messages) rt->messages->destroy(rt->messages);
    free(rt);
}

const AccountServices *account_runtime_services(const AccountRuntime *runtime) {
    return &runtime->services;
}

void account_runtime_set_label(AccountRuntime *runtime, const char *label) {
    str_copy(runtime->label, sizeof(runtime->label), label);
}
