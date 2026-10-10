#ifndef APP_MANAGERS_MESSAGING_MANAGER_DEPS_H
#define APP_MANAGERS_MESSAGING_MANAGER_DEPS_H

#include "contracts/i_awaiting_replies.h"
#include "contracts/i_chat_alert_prefs.h"
#include "contracts/i_chat_prefs_store.h"
#include "contracts/i_chat_store.h"
#include "contracts/i_contact_store.h"
#include "contracts/i_jid_alias_store.h"
#include "contracts/i_chat_exporter.h"
#include "contracts/i_event_observer.h"
#include "contracts/i_message_gateway.h"
#include "contracts/i_message_store.h"
#include "contracts/i_network_monitor.h"
#include "contracts/i_reaction_store.h"
#include "contracts/i_receipt_store.h"
#include "contracts/i_status_liker.h"
#include "contracts/i_notifier.h"
#include "core/account_id.h"
#include "core/settings.h"
#include "utilities/event_queue.h"

/* Everything the messaging manager depends on, injected by the composition root. */
typedef struct MessagingManagerDeps {
    IMessageGateway *gateway;
    IMessageStore   *messages;
    IChatStore      *chats;
    IContactStore   *contacts;
    IJidAliasStore  *aliases;
    IReactionStore  *reactions;
    IReceiptStore   *receipts;
    INotifier       *notifier;
    EventQueue      *events;
    const Settings  *settings;
    IEventObserver  *observer;       /* profile events and the like; may be NULL */
    IChatExporter   *exporter;
    INetworkMonitor *network;        /* reconnects when the adapters change; may be NULL */
    IStatusLiker    *liker;          /* private status likes; NULL sends a like as a ❤️ reply */
    AccountId        account;        /* which of your accounts this manager works for */
    const char      *account_label;  /* its label, to name it in a notification; may be NULL */
    IChatPrefsStore *chat_prefs;     /* what you chose for a chat across accounts; may be NULL */
    IChatAlertPrefs *alert_prefs;    /* sets which of a chat's messages alert you; may be NULL */
    IAwaitingReplies *awaiting;      /* the chats where your last message is unanswered; may be NULL */
} MessagingManagerDeps;

#endif
