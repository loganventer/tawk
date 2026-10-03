#include "composition/account_runtime.h"
#include "infrastructure/ifaddrs_network_monitor.h"
#include "managers/composite_event_observer.h"
#include "resource_access/caching_contact_store.h"
#include "resource_access/caching_message_store.h"
#include "resource_access/gateway_options.h"
#include "resource_access/sidecar_gateway.h"
#include "resource_access/sqlite_chat_store.h"
#include "resource_access/sqlite_contact_store.h"
#include "resource_access/sqlite_jid_alias_store.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_profile_store.h"
#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_receipt_store.h"
#include "resource_access/sqlite_scheduled_message_store.h"
#include "resource_access/sqlite_status_store.h"
#include "resource_access/whatsmeow_gateway.h"
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

/* The backend log of one account: the first keeps the file it always had. */
static void sidecar_log_path(const AccountRuntimeParams *p, AccountId id, char *out, size_t size) {
    char name[48];
    if (id <= ACCOUNT_ID_FIRST) str_copy(name, sizeof(name), "sidecar.log");
    else snprintf(name, sizeof(name), "sidecar-%d.log", id);
    path_join(out, size, p->state_dir, name);
}

/* Makes the gateway `backend` asks for, falling back to the Node.js bridge
 * on a build without whatsmeow. The views it hands out belong to it. */
static IMessageGateway *create_gateway(const AccountRuntimeParams *p, AccountId id, EventQueue *events, const char **backend_name,
                                       IProfileEditor **editor, IStatusPublisher **publisher, IStatusLiker **liker) {
    GatewayOptions options;
    memset(&options, 0, sizeof(options));
    account_runtime_auth_dir(p->settings, id, options.auth_dir, sizeof(options.auth_dir));
    path_mkdir_p(options.auth_dir, 0700);
    str_copy(options.media_dir, sizeof(options.media_dir), p->settings->media_dir);
    str_copy(options.node_binary, sizeof(options.node_binary), p->settings->node_binary);
    sidecar_log_path(p, id, options.log_path, sizeof(options.log_path));
    str_copy(options.log_dir, sizeof(options.log_dir), p->state_dir);
    str_copy(options.sidecar_dir, sizeof(options.sidecar_dir), p->sidecar_dir);
    options.debug = p->debug;

    *editor = NULL;
    *publisher = NULL;                     /* stays NULL on Baileys: it cannot post statuses */
    *liker = NULL;                         /* only Baileys can like a status privately */
    IMessageGateway *gateway = NULL;
    if (strcmp(p->backend, "baileys") != 0) {
        gateway = whatsmeow_gateway_create(&options, events);
        if (gateway) {
            *backend_name = "whatsmeow (in-process)";
            *editor = whatsmeow_gateway_profile_editor(gateway);
            *publisher = whatsmeow_gateway_status_publisher(gateway);
            return gateway;
        }
        LOG_WARN("built without whatsmeow; using the Node.js sidecar");
    }
    /* Baileys keeps its login in its own folder: its logout clears that
     * folder, which must never touch the whatsmeow login beside it. */
    GatewayOptions sidecar = options;
    path_join(sidecar.auth_dir, sizeof(sidecar.auth_dir), options.auth_dir, "baileys");
    path_mkdir_p(sidecar.auth_dir, 0700);
    gateway = sidecar_gateway_create(&sidecar, events);
    if (!gateway) return NULL;
    *backend_name = "baileys (Node.js sidecar)";
    *editor = sidecar_gateway_profile_editor(gateway);
    *liker = sidecar_gateway_status_liker(gateway);
    return gateway;
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

    rt->events = event_queue_create(EVENT_QUEUE_SIZE);
    IProfileEditor *editor = NULL;
    IStatusPublisher *publisher = NULL;
    IStatusLiker *liker = NULL;
    rt->services.backend_name = "";
    rt->gateway = rt->events ? create_gateway(p, id, rt->events, &rt->services.backend_name, &editor, &publisher, &liker) : NULL;
    if (!rt->messages || !rt->chats || !rt->contacts || !rt->aliases || !rt->reactions || !rt->receipts || !rt->profile_store ||
        !rt->status_store || !rt->scheduled_store || !rt->gateway) {
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
                                            id, rt->label };
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
