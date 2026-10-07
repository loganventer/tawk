#include "managers/messaging_manager.h"
#include "managers/chat_tally.h"
#include "managers/pending_action.h"
#include "core/live_message_ring.h"
#include "core/mention_list.h"
#include "core/mention_name.h"
#include "engines/backoff_policy.h"
#include "engines/chat_visibility.h"
#include "engines/circuit_breaker.h"
#include "engines/media_type_detector.h"
#include "engines/mention_encoder.h"
#include "engines/mention_matcher.h"
#include "engines/message_id_generator.h"
#include "engines/notification_policy.h"
#include "engines/presence_tracker.h"
#include "engines/recipient_reference_parser.h"
#include "engines/url_finder.h"
#include "engines/whatsapp_markup.h"
#include "utilities/clock_util.h"
#include "utilities/log.h"
#include "utilities/outgoing_media.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EVENTS_PER_TICK 400
#define MAX_TALLIES     256
#define MAX_TYPERS      64
#define TYPING_SHOW_MS  8000     /* how long a typing notice lasts without a refresh */
#define TYPING_RESEND_MS 7000    /* resend "composing" at most this often */
#define CONNECT_WATCHDOG_MS 45000 /* a connect that reports nothing by then is retried */
#define PENDING_READS    16       /* chats read while offline, marked read once connected */

struct MessagingManager {
    MessagingManagerDeps deps;

    AuthState auth;
    char      user_jid[128];
    char      user_name[128];
    char      pairing_code[32];
    char     *qr_ascii;

    Chat     *chats;
    int       chat_count;
    int       chats_dirty;
    char      open_jid[128];
    Message  *messages;
    int       message_count;
    int       window;               /* how many messages to keep loaded */
    int       skip;                 /* how many of the newest are left out: 0 when the window reaches the newest */
    int64_t   history_until_ms;     /* phone history request in flight until then */
    int       history_count_before;
    int       messages_dirty;

    UnreadTally tally;
    ChatTally   tallies[MAX_TALLIES];
    int         tally_count;

    char      pending_media_id[64];

    /* Who is typing where (incoming) */
    struct { char chat[128]; char sender[128]; TypingState state; int64_t until_ms; } typers[MAX_TYPERS];
    int       typer_count;
    PresenceTracker presence;           /* who is online, among the people whose chat was opened */
    int       presence_known;           /* 1 while WhatsApp tells us: connected and shown as online ourselves */

    /* Our own chat state and presence (outgoing) */
    TypingState my_typing;
    char        my_typing_chat[128];
    int64_t     my_typing_sent_ms;
    int         active;
    int         presence_sent;          /* -1 unknown, 0 offline, 1 online */

    /* Connection supervision */
    CircuitBreaker breaker;
    int            attempt;
    PendingAction  pending;
    int64_t        pending_at_ms;
    int64_t        connect_deadline_ms; /* 0 when no connect is waiting for an answer */
    int            retry_blocked;       /* outage that needs the user (replaced, banned) */

    /* Chats read while not connected: marked read on WhatsApp once connected. */
    struct { char jid[128]; int unread; } pending_reads[PENDING_READS];
    int            pending_read_count;
    char           outage_title[96];
    char           outage_detail[256];

    LiveMessageRing live;               /* new messages, for readers outside the UI */
    char           offered_jid[128];    /* a draft another client left, for the screen to pick up */
    char          *offered_text;
};

/* ---- helpers ------------------------------------------------------------ */

static BackoffPolicy backoff_of(const MessagingManager *m) {
    return (BackoffPolicy){ m->deps.settings->backoff_initial_ms, m->deps.settings->backoff_max_ms };
}

static void set_outage(MessagingManager *m, const char *title, const char *detail) {
    str_copy(m->outage_title, sizeof(m->outage_title), title);
    str_copy(m->outage_detail, sizeof(m->outage_detail), detail);
}

static void schedule(MessagingManager *m, PendingAction action, int64_t delay_ms) {
    m->pending = action;
    m->pending_at_ms = clock_now_ms() + delay_ms;
}

/* A failed attempt: count it against the breaker and back off. */
static void schedule_retry(MessagingManager *m, PendingAction action, const char *title, const char *detail) {
    int64_t now = clock_now_ms();
    circuit_breaker_record_failure(&m->breaker, now);
    set_outage(m, title, detail);
    BackoffPolicy policy = backoff_of(m);
    int64_t delay = backoff_policy_delay_ms(&policy, m->attempt++);
    int64_t breaker_wait = circuit_breaker_remaining_ms(&m->breaker, now);
    if (breaker_wait > delay) delay = breaker_wait;
    schedule(m, action, delay);
    LOG_WARN("connection: %s; retry %d in %lld ms (breaker %s)", detail, m->attempt,
             (long long)delay, circuit_state_name(m->breaker.state));
}

static void send_presence(MessagingManager *m) {
    int want = m->deps.settings->appear_online && m->active;
    if (m->auth != AUTH_STATE_CONNECTED || want == m->presence_sent) return;
    m->deps.gateway->presence(m->deps.gateway, want);
    m->presence_sent = want;
}

/* Marks `jid` read on WhatsApp: read receipts for its `unread` newest
 * incoming messages (when receipts are on), and the account-wide read mark
 * that clears its badge on your phone. The ids come from the database, so
 * this works for messages from before a restart or from history sync. */
static void send_read(MessagingManager *m, const char *jid, int unread) {
    if (!jid || !jid[0]) return;
    if (m->auth != AUTH_STATE_CONNECTED) {                  /* keep it for when the connection is back */
        for (int i = 0; i < m->pending_read_count; i++) {
            if (strcmp(m->pending_reads[i].jid, jid) == 0) { m->pending_reads[i].unread += unread; return; }
        }
        if (m->pending_read_count < PENDING_READS) {
            str_copy(m->pending_reads[m->pending_read_count].jid, sizeof(m->pending_reads[0].jid), jid);
            m->pending_reads[m->pending_read_count++].unread = unread;
        }
        return;
    }
    ReadRequest r;
    memset(&r, 0, sizeof(r));
    str_copy(r.chat_jid, sizeof(r.chat_jid), jid);
    r.send_receipts = m->deps.settings->send_read_receipts;
    int want = unread > 0 ? unread : 1;
    if (want > READ_REQUEST_MAX) want = READ_REQUEST_MAX;
    Message *msgs = NULL;
    int n = 0;
    if (m->deps.messages->recent(m->deps.messages, jid, want + 32, &msgs, &n) != 0) return;
    if (n > 0) {                                            /* newest last */
        const Message *last = &msgs[n - 1];
        str_copy(r.last_id, sizeof(r.last_id), last->id);
        str_copy(r.last_sender, sizeof(r.last_sender), last->sender_jid);
        r.last_from_me = last->from_me;
        r.last_timestamp = last->timestamp;
    }
    for (int i = n - 1; i >= 0 && r.count < want; i--) {
        if (msgs[i].from_me || msgs[i].deleted) continue;
        str_copy(r.items[r.count].id, sizeof(r.items[0].id), msgs[i].id);
        str_copy(r.items[r.count].sender, sizeof(r.items[0].sender), msgs[i].sender_jid);
        r.count++;
    }
    message_array_free(msgs, n);
    if (r.last_id[0]) m->deps.gateway->mark_read(m->deps.gateway, &r);
}

static void flush_pending_reads(MessagingManager *m) {
    int count = m->pending_read_count;
    m->pending_read_count = 0;
    for (int i = 0; i < count; i++) send_read(m, m->pending_reads[i].jid, m->pending_reads[i].unread);
}

static void mark_connected(MessagingManager *m) {
    circuit_breaker_record_success(&m->breaker);
    m->attempt = 0;
    m->pending = PENDING_NONE;
    m->retry_blocked = 0;
    m->auth = AUTH_STATE_CONNECTED;
    m->outage_title[0] = m->outage_detail[0] = '\0';
    m->presence_sent = -1;
    send_presence(m);
    if (m->open_jid[0] && !chat_jid_is_group(m->open_jid)) m->deps.gateway->subscribe(m->deps.gateway, m->open_jid);
    flush_pending_reads(m);
}

static ChatTally *find_tally(MessagingManager *m, const char *jid, int create) {
    for (int i = 0; i < m->tally_count; i++) if (strcmp(m->tallies[i].jid, jid) == 0) return &m->tallies[i];
    if (!create || m->tally_count >= MAX_TALLIES) return NULL;
    ChatTally *t = &m->tallies[m->tally_count++];
    memset(t, 0, sizeof(*t));
    str_copy(t->jid, sizeof(t->jid), jid);
    return t;
}

static void clear_tally(MessagingManager *m, const char *jid) {
    for (int i = 0; i < m->tally_count; i++) {
        if (strcmp(m->tallies[i].jid, jid) != 0) continue;
        for (int k = 0; k < MESSAGE_TYPE_COUNT; k++) m->tally.counts[k] -= m->tallies[i].tally.counts[k];
        m->tallies[i] = m->tallies[--m->tally_count];
        return;
    }
}

int messaging_manager_message_receipts(MessagingManager *m, const char *message_id, Receipt *out, int max) {
    if (!message_id || !message_id[0] || max <= 0) return 0;
    int n = m->deps.receipts->list(m->deps.receipts, message_id, out, max);
    for (int i = 0; i < n; i++) messaging_manager_display_name(m, out[i].jid, out[i].name, sizeof(out[i].name));
    return n;
}

void messaging_manager_display_name(MessagingManager *m, const char *jid, char *out, unsigned long size) {
    jid = m->deps.aliases->resolve(m->deps.aliases, jid);
    Contact c;
    if (m->deps.contacts->get(m->deps.contacts, jid, &c) == 0) contact_display_name(&c, out, size);
    else contact_phone_from_jid(jid, out, size);
}

int messaging_manager_find_contacts(MessagingManager *m, const char *ref, Contact *out, int max) {
    if (!ref || !*ref || !out || max <= 0) return 0;
    char jid[128];
    if (recipient_reference_parse(ref, jid, sizeof(jid)) == 0) {
        return m->deps.contacts->get(m->deps.contacts, jid, &out[0]) == 0 ? 1 : 0;
    }
    if (!m->deps.contacts->find_by_name) return 0;
    int n = m->deps.contacts->find_by_name(m->deps.contacts, ref, out, max);
    return n > 0 ? n : 0;
}

/* The names mentions in `msg` show: yours as linked, anyone else's as saved. */
static int mention_names(MessagingManager *m, const Message *msg, MentionName *names) {
    MentionList mentions;
    mention_list_parse(&mentions, msg->mentions);
    for (int k = 0; k < mentions.count; k++) {
        str_copy(names[k].user, sizeof(names[k].user), mentions.items[k].user);
        if (m->user_jid[0] && strcmp(mentions.items[k].jid, m->user_jid) == 0 && m->user_name[0]) {
            str_copy(names[k].name, sizeof(names[k].name), m->user_name);
        } else {
            messaging_manager_display_name(m, mentions.items[k].jid, names[k].name, sizeof(names[k].name));
        }
    }
    return mentions.count;
}

int messaging_manager_format_message(MessagingManager *m, const Message *msg, StyledText *out) {
    styled_text_init(out);
    if (!m->deps.settings->format_text || !msg || msg->deleted || !msg->text || !msg->text[0]) return -1;
    MentionName names[MENTION_LIST_MAX];
    int n = mention_names(m, msg, names);
    return whatsapp_markup_parse(msg->text, names, n, out);
}

void messaging_manager_message_preview(MessagingManager *m, const Message *msg, char *out, size_t size) {
    if (!m->deps.settings->format_text || !msg->text || !msg->text[0] || msg->deleted) {
        message_preview(msg, out, size);
        return;
    }
    MentionName names[MENTION_LIST_MAX];
    int n = mention_names(m, msg, names);
    char plain[2048];
    whatsapp_markup_plain(msg->text, names, n, plain, sizeof(plain));
    Message shown = *msg;                               /* the same message, with its text as shown */
    shown.text = plain;
    message_preview(&shown, out, size);
}

static void typing_text(MessagingManager *m, const Chat *c, char *out, size_t size) {
    out[0] = '\0';
    int64_t now = clock_now_ms();
    for (int i = 0; i < m->typer_count; i++) {
        if (m->typers[i].until_ms <= now || strcmp(m->typers[i].chat, c->jid) != 0) continue;
        const char *verb = m->typers[i].state == TYPING_RECORDING ? "recording audio" : "typing";
        if (c->is_group && m->typers[i].sender[0]) {
            char who[40];
            messaging_manager_display_name(m, m->typers[i].sender, who, sizeof(who));
            snprintf(out, size, "%s is %s\xE2\x80\xA6", who, verb);
        } else {
            snprintf(out, size, "%s\xE2\x80\xA6", verb);
        }
        return;
    }
}

/* Whether the other person is online, where WhatsApp has told us. */
static void fill_presence(MessagingManager *m, Chat *c) {
    const ContactPresence *p = c->is_group || !m->presence_known ? NULL : presence_tracker_find(&m->presence, c->jid);
    c->presence = p ? p->state : PRESENCE_UNKNOWN;
    c->last_seen = p ? p->last_seen : 0;
}

static void rebuild_chats(MessagingManager *m) {
    free(m->chats);
    m->chats = NULL;
    m->chat_count = 0;
    m->deps.chats->get_all(m->deps.chats, &m->chats, &m->chat_count);
    int kept = 0;                                        /* drop Status and other broadcast feeds */
    for (int i = 0; i < m->chat_count; i++) {
        if (!chat_visibility_hidden(m->chats[i].jid)) m->chats[kept++] = m->chats[i];
    }
    m->chat_count = kept;
    for (int i = 0; i < m->chat_count; i++) {
        Chat *c = &m->chats[i];
        typing_text(m, c, c->typing, sizeof(c->typing));
        fill_presence(m, c);
        if (c->name[0]) continue;
        if (c->is_group) str_copy(c->name, sizeof(c->name), "Group");
        else messaging_manager_display_name(m, c->jid, c->name, sizeof(c->name));
    }
    qsort(m->chats, (size_t)m->chat_count, sizeof(Chat), chat_compare);
    m->chats_dirty = 0;
}

/* How many messages are kept either side of the ones on screen. */
static int window_margin(const MessagingManager *m) {
    int margin = m->deps.settings->message_margin;
    return margin < 10 ? 10 : margin;
}

/* Whether the message is among those loaded for the open chat. Ticks and
 * receipts for any other message change nothing on the screen. */
static int showing(const MessagingManager *m, const char *id) {
    for (int i = 0; i < m->message_count; i++) {
        if (strcmp(m->messages[i].id, id) == 0) return 1;
    }
    return 0;
}

/* One of your messages was read: those who follow along hear of it. */
static void note_read(MessagingManager *m, const Event *e) {
    Message msg;
    if (m->deps.messages->get(m->deps.messages, e->id, &msg) != 0) return;
    if (msg.from_me) live_message_ring_note(&m->live, LIVE_KIND_READ, msg.id, msg.chat_jid, m->deps.aliases->resolve(m->deps.aliases, e->jid), "", e->at);
    message_dispose(&msg);
}

/* Someone else reacted to one of your messages, or took the reaction back. */
static void note_reaction(MessagingManager *m, const Event *e) {
    if (m->user_jid[0] && strcmp(e->jid, m->user_jid) == 0) return;      /* your own reaction */
    Message msg;
    if (m->deps.messages->get(m->deps.messages, e->id, &msg) != 0) return;
    if (msg.from_me) live_message_ring_note(&m->live, LIVE_KIND_REACTION, msg.id, msg.chat_jid, e->jid, e->emoji, (int64_t)time(NULL));
    message_dispose(&msg);
}

/* Someone else changed or deleted a message they sent. Called once the store holds the change. */
static void note_edit(MessagingManager *m, const Event *e) {
    Message msg;
    if (m->deps.messages->get(m->deps.messages, e->message.id, &msg) != 0) return;
    if (!msg.from_me) {
        live_message_ring_note(&m->live, e->message.deleted ? LIVE_KIND_DELETE : LIVE_KIND_EDIT, msg.id, msg.chat_jid,
                               msg.sender_jid[0] ? msg.sender_jid : msg.chat_jid, "", (int64_t)time(NULL));
    }
    message_dispose(&msg);
}

static void reload_messages(MessagingManager *m) {
    message_array_free(m->messages, m->message_count);
    m->messages = NULL;
    m->message_count = 0;
    if (m->open_jid[0]) {
        if (m->window < window_margin(m)) m->window = window_margin(m);
        m->deps.messages->slice(m->deps.messages, m->open_jid, m->skip, m->window, &m->messages, &m->message_count);
        if (m->message_count == 0 && m->skip > 0) {          /* the window slid past what the chat holds */
            message_array_free(m->messages, m->message_count);
            m->skip = 0;
            m->deps.messages->slice(m->deps.messages, m->open_jid, 0, m->window, &m->messages, &m->message_count);
        }
        if (m->history_until_ms && m->message_count > m->history_count_before) m->history_until_ms = 0;
        for (int i = 0; i < m->message_count; i++) {
            m->deps.reactions->summary(m->deps.reactions, m->messages[i].id, m->messages[i].reactions,
                                       sizeof(m->messages[i].reactions));
        }
    }
    m->messages_dirty = 0;
}

static int should_auto_download(const MessagingManager *m, const Message *msg) {
    const Settings *s = m->deps.settings;
    return s->auto_download_media && msg->media_ref &&
           (msg->type == MESSAGE_TYPE_IMAGE || msg->type == MESSAGE_TYPE_VIDEO ||
            msg->type == MESSAGE_TYPE_AUDIO || msg->type == MESSAGE_TYPE_STICKER);
}

/* ---- event handlers ----------------------------------------------------- */

static void on_message(MessagingManager *m, Event *e, ManagerChanges *ch) {
    Message *msg = &e->message;
    Message existing;
    int existed = m->deps.messages->get(m->deps.messages, msg->id, &existing) == 0;
    if (existed) message_dispose(&existing);

    if (!msg->from_me && msg->sender_name[0]) {
        Contact c;
        contact_init(&c, msg->sender_jid);
        str_copy(c.push_name, sizeof(c.push_name), msg->sender_name);
        m->deps.contacts->upsert(m->deps.contacts, &c);
    }
    m->deps.messages->save(m->deps.messages, msg);

    char preview[256], line[512];
    messaging_manager_message_preview(m, msg, preview, sizeof(preview));
    if (chat_jid_is_group(msg->chat_jid) && !msg->from_me && msg->sender_name[0]) {
        snprintf(line, sizeof(line), "%s: %s", msg->sender_name, preview);
    } else {
        str_copy(line, sizeof(line), preview);
    }
    m->deps.chats->touch(m->deps.chats, msg->chat_jid, msg->timestamp, line);
    m->chats_dirty = 1;
    if (strcmp(m->open_jid, msg->chat_jid) == 0) {
        m->messages_dirty = 1;
        if (!existed && m->skip > 0) m->skip++;              /* scrolled back: the window stays where it is */
    }

    if (!existed && should_auto_download(m, msg)) {
        m->deps.gateway->download_media(m->deps.gateway, msg->id, msg->media_ref, m->deps.settings->auto_download_max_mb);
    }
    if (!existed && e->live) live_message_ring_push(&m->live, msg->id, msg->chat_jid);
    if (existed || !e->live || msg->from_me) return;

    ch->live_message = 1;
    for (int i = 0; i < m->typer_count; i++) {
        if (!strcmp(m->typers[i].chat, msg->chat_jid) && !strcmp(m->typers[i].sender, msg->sender_jid)) {
            m->typers[i--] = m->typers[--m->typer_count];
        }
    }
    int is_open = strcmp(m->open_jid, msg->chat_jid) == 0;
    if (is_open) {
        send_read(m, msg->chat_jid, 1);
    } else {
        m->deps.chats->add_unread(m->deps.chats, msg->chat_jid, 1);
        if (msg->mentions_me) m->deps.chats->mark_mention(m->deps.chats, msg->chat_jid);
    }

    Chat chat;
    int have_chat = m->deps.chats->get(m->deps.chats, msg->chat_jid, &chat) == 0;
    if (!notification_policy_should_notify(m->deps.settings, have_chat ? &chat : NULL, msg, e->live, is_open)) return;

    ChatTally *t = find_tally(m, msg->chat_jid, 1);
    if (t) t->tally.counts[msg->type]++;
    m->tally.counts[msg->type]++;

    Notification n;
    memset(&n, 0, sizeof(n));
    str_copy(n.chat_jid, sizeof(n.chat_jid), msg->chat_jid);
    n.type = msg->type;
    n.is_group = chat_jid_is_group(msg->chat_jid);
    if (have_chat && chat.name[0]) str_copy(n.title, sizeof(n.title), chat.name);
    else messaging_manager_display_name(m, msg->chat_jid, n.title, sizeof(n.title));
    if (m->deps.settings->show_preview) str_copy(n.body, sizeof(n.body), line);
    if (have_chat) str_copy(n.tone, sizeof(n.tone), chat.tone);
    n.account = m->deps.account;
    if (m->deps.account_label) str_copy(n.account_label, sizeof(n.account_label), m->deps.account_label);
    m->deps.notifier->notify(m->deps.notifier, &n);
    ch->notified++;
}

static void on_connection(MessagingManager *m, const Event *e, ManagerChanges *ch) {
    ch->connection = 1;
    const char *r = e->reason;
    if (strcmp(r, "connecting") != 0) m->connect_deadline_ms = 0;   /* the backend answered */
    if (!strcmp(r, "open")) {
        mark_connected(m);
        ch->auth = 1;
    } else if (!strcmp(r, "connecting")) {
        /* informational */
    } else if (!strcmp(r, "restart_required")) {
        schedule(m, PENDING_CONNECT, 0);        /* expected right after pairing, not a failure */
    } else if (!strcmp(r, "qr_timeout")) {
        m->auth = AUTH_STATE_NEEDS_LOGIN;
        ch->auth = 1;
    } else if (!strcmp(r, "replaced") || !strcmp(r, "banned") || !strcmp(r, "outdated")) {
        m->auth = AUTH_STATE_RECONNECTING;
        m->retry_blocked = 1;
        m->pending = PENDING_NONE;
        set_outage(m, !strcmp(r, "replaced") ? "Signed in elsewhere" : "WhatsApp is unavailable", e->detail);
    } else {
        if (m->auth == AUTH_STATE_CONNECTED) m->auth = AUTH_STATE_RECONNECTING;
        schedule_retry(m, PENDING_CONNECT, "Connection lost", e->detail[0] ? e->detail : "The connection to WhatsApp dropped.");
    }
}

static void on_media_ready(MessagingManager *m, const Event *e, ManagerChanges *ch) {
    /* Only accept files inside our media folder; the backend is not trusted with paths. */
    if (!path_is_within(e->path, m->deps.settings->media_dir) || !path_is_regular_file(e->path)) {
        LOG_WARN("rejected media path outside the media folder");
        return;
    }
    m->deps.messages->set_media_path(m->deps.messages, e->id, e->path);
    m->messages_dirty = 1;
    Message ready;
    if (m->deps.messages->get(m->deps.messages, e->id, &ready) == 0) {
        /* Readers that follow along hear that the file is there now. */
        live_message_ring_note(&m->live, LIVE_KIND_MEDIA_READY, e->id, ready.chat_jid, "", "", (int64_t)time(NULL));
        message_dispose(&ready);
    }
    if (strcmp(m->pending_media_id, e->id) == 0) {
        Message msg;
        MessageType type = MESSAGE_TYPE_OTHER;
        if (m->deps.messages->get(m->deps.messages, e->id, &msg) == 0) { type = msg.type; message_dispose(&msg); }
        str_copy(ch->media_id, sizeof(ch->media_id), e->id);
        str_copy(ch->media_path, sizeof(ch->media_path), e->path);
        ch->media_type = type;
        m->pending_media_id[0] = '\0';
    }
}

/* Rewrites a JID in place to its canonical form (phone number, not LID). */
static void canonical(MessagingManager *m, char *jid, size_t size) {
    const char *resolved = m->deps.aliases->resolve(m->deps.aliases, jid);
    if (resolved != jid && strcmp(resolved, jid) != 0) str_copy(jid, size, resolved);
}

/* Mentioned people by their phone-number JID where known; the digits in
 * the text stay as they were written. */
static void canonical_mentions(MessagingManager *m, Message *msg) {
    if (!msg->mentions) return;
    MentionList list;
    mention_list_parse(&list, msg->mentions);
    for (int i = 0; i < list.count; i++) canonical(m, list.items[i].jid, sizeof(list.items[i].jid));
    char *text = mention_list_serialize(&list);
    message_set_mentions(msg, text);
    free(text);
}

static void normalise(MessagingManager *m, Event *e) {
    switch (e->type) {
        case EVENT_MESSAGE_UPSERT:
            canonical(m, e->message.chat_jid, sizeof(e->message.chat_jid));
            canonical(m, e->message.sender_jid, sizeof(e->message.sender_jid));
            canonical_mentions(m, &e->message);
            break;
        case EVENT_CHAT_REMOVED:
        case EVENT_CHAT_UPDATE:    canonical(m, e->chat.jid, sizeof(e->chat.jid)); break;
        case EVENT_CONTACT_UPDATE: canonical(m, e->contact.jid, sizeof(e->contact.jid)); break;
        case EVENT_MESSAGE_REMOVED:
        case EVENT_MESSAGE_EDIT:
            canonical(m, e->message.chat_jid, sizeof(e->message.chat_jid));
            break;
        case EVENT_REACTION:
        case EVENT_TYPING:
        case EVENT_PRESENCE:
            canonical(m, e->chat.jid, sizeof(e->chat.jid));
            canonical(m, e->jid, sizeof(e->jid));
            break;
        default: break;
    }
}

static void on_typing(MessagingManager *m, const Event *e) {
    TypingState state = !strcmp(e->state, "composing") ? TYPING_COMPOSING :
                        !strcmp(e->state, "recording") ? TYPING_RECORDING : TYPING_PAUSED;
    int slot = -1;
    for (int i = 0; i < m->typer_count; i++) {
        if (!strcmp(m->typers[i].chat, e->chat.jid) && !strcmp(m->typers[i].sender, e->jid)) { slot = i; break; }
    }
    if (state == TYPING_PAUSED) {
        if (slot >= 0) m->typers[slot] = m->typers[--m->typer_count];
    } else {
        if (slot < 0 && m->typer_count < MAX_TYPERS) slot = m->typer_count++;
        if (slot < 0) return;
        str_copy(m->typers[slot].chat, sizeof(m->typers[slot].chat), e->chat.jid);
        str_copy(m->typers[slot].sender, sizeof(m->typers[slot].sender), e->jid);
        m->typers[slot].state = state;
        m->typers[slot].until_ms = clock_now_ms() + TYPING_SHOW_MS;
    }
    m->chats_dirty = 1;
}

/* Someone whose chat was opened came online or left. Those who follow along
 * hear of it only when it is a change, so a repeated notice says nothing twice. */
static void on_presence(MessagingManager *m, const Event *e) {
    PresenceState state = presence_state_parse(e->state);
    if (chat_jid_is_group(e->jid) || !presence_tracker_note(&m->presence, e->jid, state, e->at)) return;
    m->chats_dirty = 1;
    live_message_ring_note(&m->live, LIVE_KIND_PRESENCE, "", e->jid, e->jid, presence_state_name(state), e->at);
}

/* WhatsApp only says who is online while we are connected and shown as online
 * ourselves. Outside that nothing heard earlier still holds, so it is dropped. */
static void watch_presence(MessagingManager *m) {
    int known = m->auth == AUTH_STATE_CONNECTED && m->presence_sent == 1;
    if (known == m->presence_known) return;
    m->presence_known = known;
    if (!known) presence_tracker_reset(&m->presence);
    m->chats_dirty = 1;
}

/* Drops typing notices that were not refreshed in time. */
static void expire_typers(MessagingManager *m) {
    int64_t now = clock_now_ms();
    for (int i = 0; i < m->typer_count; i++) {
        if (m->typers[i].until_ms > now) continue;
        m->typers[i--] = m->typers[--m->typer_count];
        m->chats_dirty = 1;
    }
}

/* A LID turned out to be a phone number we know: fold everything stored
 * under the LID into the phone-number chat and contact. */
static void on_alias(MessagingManager *m, const Event *e, ManagerChanges *ch) {
    if (m->deps.aliases->put(m->deps.aliases, e->lid, e->jid) <= 0) return;
    m->deps.messages->reassign_jid(m->deps.messages, e->lid, e->jid);
    m->deps.chats->merge(m->deps.chats, e->lid, e->jid);
    m->deps.contacts->merge(m->deps.contacts, e->lid, e->jid);
    m->deps.reactions->reassign_sender(m->deps.reactions, e->lid, e->jid);
    m->deps.receipts->reassign_jid(m->deps.receipts, e->lid, e->jid);
    ChatTally *from = find_tally(m, e->lid, 0);
    if (from) {
        ChatTally *to = find_tally(m, e->jid, 1);
        if (to) for (int k = 0; k < MESSAGE_TYPE_COUNT; k++) to->tally.counts[k] += from->tally.counts[k];
        *from = m->tallies[--m->tally_count];
    }
    if (strcmp(m->open_jid, e->lid) == 0) {
        str_copy(m->open_jid, sizeof(m->open_jid), e->jid);
        ch->messages = 1;
    }
    m->chats_dirty = m->messages_dirty = 1;
}

/* The chat an event belongs to, for events that carry one. */
static const char *event_chat(const Event *e) {
    switch (e->type) {
        case EVENT_MESSAGE_UPSERT:
        case EVENT_MESSAGE_EDIT:
        case EVENT_MESSAGE_REMOVED: return e->message.chat_jid;
        case EVENT_CHAT_REMOVED:    return e->chat.jid;
        case EVENT_CHAT_UPDATE:
        case EVENT_REACTION:
        case EVENT_TYPING:
        case EVENT_PRESENCE:       return e->chat.jid;
        default:                   return NULL;
    }
}

static void forget_chat(MessagingManager *m, const char *jid, ManagerChanges *ch);

/* Statuses arrive as messages on status@broadcast. They go to the status
 * feed (through the observer) and never become a chat, a notification or
 * an unread count. */
static int is_status(const Event *e) {
    const char *chat = event_chat(e);
    return chat && strcmp(chat, "status@broadcast") == 0 &&
           (e->type == EVENT_MESSAGE_UPSERT || e->type == EVENT_MESSAGE_EDIT || e->type == EVENT_MESSAGE_REMOVED ||
            e->type == EVENT_REACTION);                   /* likes of your statuses */
}

static void handle_event(MessagingManager *m, Event *e, ManagerChanges *ch) {
    normalise(m, e);
    if (is_status(e)) {
        if (m->deps.observer && m->deps.observer->on_event(m->deps.observer, e)) ch->statuses = 1;
        return;
    }
    if (chat_visibility_hidden(event_chat(e))) return;
    if (e->type == EVENT_PROFILE || e->type == EVENT_PICTURE || e->type == EVENT_PICTURE_CHANGED ||
        e->type == EVENT_BLOCKLIST || e->type == EVENT_CALL || e->type == EVENT_PROFILE_UPDATED ||
        e->type == EVENT_STATUS_POSTED) {
        if (m->deps.observer && m->deps.observer->on_event(m->deps.observer, e)) ch->profiles = 1;
        return;
    }
    switch (e->type) {
        case EVENT_JID_ALIAS:
            on_alias(m, e, ch);
            if (m->deps.observer) m->deps.observer->on_event(m->deps.observer, e);   /* others keep JIDs too */
            break;
        case EVENT_CHAT_REMOVED:
            forget_chat(m, e->chat.jid, ch);
            break;
        case EVENT_MESSAGE_REMOVED:
            m->deps.messages->remove(m->deps.messages, e->message.id);
            m->messages_dirty = 1;
            break;
        case EVENT_LINK_PREVIEW: {                    /* the card made for a message you sent */
            Message stored;
            if (m->deps.messages->get(m->deps.messages, e->message.id, &stored) == 0) {
                message_set_link(&stored, link_preview_copy(e->message.link));
                message_set_thumbnail(&stored, e->message.thumbnail, e->message.thumbnail_len);
                m->deps.messages->save(m->deps.messages, &stored);
                if (strcmp(m->open_jid, stored.chat_jid) == 0) m->messages_dirty = 1;
                message_dispose(&stored);
            }
            break;
        }
        case EVENT_MESSAGE_EDIT:
            m->deps.messages->edit_text(m->deps.messages, e->message.id, e->message.text, e->message.deleted);
            if (e->live) note_edit(m, e);
            if (strcmp(m->open_jid, e->message.chat_jid) == 0 || !e->message.chat_jid[0]) m->messages_dirty = 1;
            break;
        case EVENT_REACTION:
            m->deps.reactions->put(m->deps.reactions, e->id, e->jid, e->emoji);
            if (e->live) note_reaction(m, e);
            if (strcmp(m->open_jid, e->chat.jid) == 0) m->messages_dirty = 1;
            break;
        case EVENT_TYPING:
            on_typing(m, e);
            break;
        case EVENT_PRESENCE:
            on_presence(m, e);
            break;
        case EVENT_MESSAGE_UPSERT:
            on_message(m, e, ch);
            break;
        case EVENT_MESSAGE_STATUS:
            m->deps.messages->update_status(m->deps.messages, e->id, e->status);
            if (showing(m, e->id)) m->messages_dirty = 1;
            break;
        case EVENT_MESSAGE_RECEIPT:
            m->deps.receipts->put(m->deps.receipts, e->id, m->deps.aliases->resolve(m->deps.aliases, e->jid), e->receipt, e->at);
            if (e->receipt == RECEIPT_READ) note_read(m, e);
            if (showing(m, e->id)) m->messages_dirty = 1;   /* an open message info panel shows it */
            break;
        case EVENT_CHAT_UPDATE:
            m->deps.chats->upsert(m->deps.chats, &e->chat);
            m->chats_dirty = 1;
            break;
        case EVENT_CONTACT_UPDATE:
            m->deps.contacts->upsert(m->deps.contacts, &e->contact);
            m->chats_dirty = 1;
            break;
        case EVENT_AUTH_REQUIRED:
            m->auth = AUTH_STATE_NEEDS_LOGIN;
            ch->auth = 1;
            break;
        case EVENT_AUTH_QR:
            free(m->qr_ascii);
            m->qr_ascii = e->qr_ascii;
            e->qr_ascii = NULL;
            m->auth = AUTH_STATE_NEEDS_LOGIN;
            ch->auth = 1;
            break;
        case EVENT_AUTH_PAIRING_CODE:
            str_copy(m->pairing_code, sizeof(m->pairing_code), e->code);
            ch->auth = 1;
            break;
        case EVENT_AUTH_CONNECTED:
            str_copy(m->user_jid, sizeof(m->user_jid), e->jid);
            if (e->name[0]) str_copy(m->user_name, sizeof(m->user_name), e->name);
            m->pairing_code[0] = '\0';
            free(m->qr_ascii);
            m->qr_ascii = NULL;
            mark_connected(m);
            ch->auth = 1;
            /* the account manager keeps who is linked too */
            if (m->deps.observer && m->deps.observer->on_event(m->deps.observer, e)) ch->profiles = 1;
            break;
        case EVENT_AUTH_LOGGED_OUT:
            m->auth = AUTH_STATE_NEEDS_LOGIN;
            m->user_jid[0] = m->user_name[0] = m->pairing_code[0] = '\0';
            schedule(m, PENDING_CONNECT, 0);    /* reconnect to obtain a fresh QR code */
            ch->auth = 1;
            break;
        case EVENT_CONNECTION_STATUS:
            on_connection(m, e, ch);
            break;
        case EVENT_MEDIA_READY:
            on_media_ready(m, e, ch);
            /* It may be a status's photo or video instead of a message's. */
            if (m->deps.observer && m->deps.observer->on_event(m->deps.observer, e)) ch->statuses = 1;
            break;
        case EVENT_SIDECAR_EXITED:
            m->auth = m->auth == AUTH_STATE_NEEDS_LOGIN ? m->auth : AUTH_STATE_RECONNECTING;
            schedule_retry(m, PENDING_RESTART, "WhatsApp bridge stopped", e->detail);
            ch->connection = 1;
            break;
        case EVENT_ERROR:
            str_copy(ch->error, sizeof(ch->error), e->detail);
            if (e->id[0] && strcmp(e->id, m->pending_media_id) == 0) m->pending_media_id[0] = '\0';
            break;
        default:
            break;
    }
}

static void run_supervisor(MessagingManager *m, ManagerChanges *ch) {
    if (m->pending == PENDING_NONE || clock_now_ms() < m->pending_at_ms) return;
    if (!circuit_breaker_allow(&m->breaker, clock_now_ms())) return;
    PendingAction action = m->pending;
    m->pending = PENDING_NONE;
    ch->connection = 1;
    if (action == PENDING_RESTART) {
        m->deps.gateway->stop(m->deps.gateway);
        if (m->deps.gateway->start(m->deps.gateway) != 0) {
            schedule_retry(m, PENDING_RESTART, "WhatsApp bridge unavailable", "The WhatsApp bridge could not be started.");
            return;
        }
    }
    int rc = action == PENDING_RECONNECT ? m->deps.gateway->reconnect(m->deps.gateway)
                                         : m->deps.gateway->connect(m->deps.gateway);
    if (rc != 0) {
        schedule_retry(m, PENDING_CONNECT, "Connection lost", "The WhatsApp bridge did not take the connect request.");
        return;
    }
    m->connect_deadline_ms = clock_now_ms() + CONNECT_WATCHDOG_MS;
}

/* A connect the backend never answered (no "open", no failure) would leave
 * the supervisor idle forever, so it counts as a failure after a while. */
static void run_connect_watchdog(MessagingManager *m, ManagerChanges *ch) {
    if (!m->connect_deadline_ms || clock_now_ms() < m->connect_deadline_ms || m->pending != PENDING_NONE) return;
    m->connect_deadline_ms = 0;
    if (m->auth == AUTH_STATE_NEEDS_LOGIN) return;   /* waiting for a QR scan is not a hang */
    ch->connection = 1;
    if (m->auth == AUTH_STATE_CONNECTED) m->auth = AUTH_STATE_RECONNECTING;
    schedule_retry(m, PENDING_RECONNECT, "Connection lost", "WhatsApp did not answer the connection attempt in time.");
}

/* After the adapters change, the old connection is usually dead without
 * knowing it. Start over at once, with a fresh backoff and breaker. */
static void watch_network(MessagingManager *m, ManagerChanges *ch) {
    INetworkMonitor *net = m->deps.network;
    if (!net || !net->changed(net, clock_now_ms())) return;
    if (m->retry_blocked || m->auth == AUTH_STATE_STARTING || m->auth == AUTH_STATE_FAILED ||
        m->auth == AUTH_STATE_NEEDS_LOGIN) return;
    LOG_INFO("connection: network changed; reconnecting");
    circuit_breaker_init(&m->breaker, m->deps.settings->breaker_threshold, m->deps.settings->breaker_cooldown_s);
    m->attempt = 0;
    m->connect_deadline_ms = 0;
    if (m->auth == AUTH_STATE_CONNECTED) m->auth = AUTH_STATE_RECONNECTING;
    set_outage(m, "Network changed", "Reconnecting on the new network.");
    schedule(m, PENDING_RECONNECT, 0);
    ch->connection = 1;
    ch->auth = 1;
}

/* ---- public API --------------------------------------------------------- */

MessagingManager *messaging_manager_create(const MessagingManagerDeps *deps) {
    MessagingManager *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->deps = *deps;
    live_message_ring_init(&m->live);
    presence_tracker_init(&m->presence);
    m->auth = AUTH_STATE_STARTING;
    m->active = 1;
    m->presence_sent = -1;
    circuit_breaker_init(&m->breaker, deps->settings->breaker_threshold, deps->settings->breaker_cooldown_s);
    rebuild_chats(m);
    return m;
}

void messaging_manager_destroy(MessagingManager *m) {
    if (!m) return;
    free(m->offered_text);
    free(m->chats);
    message_array_free(m->messages, m->message_count);
    free(m->qr_ascii);
    free(m);
}

void messaging_manager_start(MessagingManager *m) {
    if (m->deps.gateway->start(m->deps.gateway) != 0) {
        m->auth = AUTH_STATE_RECONNECTING;
        schedule_retry(m, PENDING_RESTART, "WhatsApp bridge unavailable", "The WhatsApp bridge could not be started.");
        return;
    }
    m->deps.gateway->connect(m->deps.gateway);
    m->connect_deadline_ms = clock_now_ms() + CONNECT_WATCHDOG_MS;   /* the first connect is watched too */
}

void messaging_manager_tick(MessagingManager *m, ManagerChanges *ch) {
    memset(ch, 0, sizeof(*ch));
    Event evt;
    for (int i = 0; i < EVENTS_PER_TICK && event_queue_pop(m->deps.events, &evt) == 0; i++) {
        handle_event(m, &evt, ch);
        event_dispose(&evt);
    }
    watch_network(m, ch);
    run_supervisor(m, ch);
    run_connect_watchdog(m, ch);
    watch_presence(m);
    expire_typers(m);
    if (m->history_until_ms && clock_now_ms() > m->history_until_ms) {
        m->history_until_ms = 0;
        str_copy(ch->error, sizeof(ch->error), "Your phone sent no older messages for this chat");
    }
    if (m->my_typing == TYPING_COMPOSING && clock_now_ms() - m->my_typing_sent_ms > TYPING_RESEND_MS * 2) {
        messaging_manager_set_typing(m, TYPING_PAUSED);          /* stopped typing a while ago */
    }
    if (m->chats_dirty) { rebuild_chats(m); ch->chats = 1; }
    if (m->messages_dirty) { reload_messages(m); ch->messages = 1; }
}

void messaging_manager_retry_now(MessagingManager *m) {
    if (m->auth == AUTH_STATE_CONNECTED) return;
    m->retry_blocked = 0;
    circuit_breaker_init(&m->breaker, m->deps.settings->breaker_threshold, m->deps.settings->breaker_cooldown_s);
    /* Reconnect rather than connect: a connection that still looks open
     * may be the reason the user is pressing retry. */
    if (m->pending == PENDING_NONE || m->pending == PENDING_CONNECT) m->pending = PENDING_RECONNECT;
    m->connect_deadline_ms = 0;
    m->pending_at_ms = 0;
}

const Chat *messaging_manager_chats(MessagingManager *m, int *count) {
    *count = m->chat_count;
    return m->chats;
}

void messaging_manager_open_chat(MessagingManager *m, const char *jid) {
    messaging_manager_set_typing(m, TYPING_PAUSED);
    if (!jid || strcmp(jid, m->open_jid) != 0) {
        m->window = window_margin(m) + window_margin(m) / 2;
        m->skip = 0;
        m->history_until_ms = 0;
    }
    str_copy(m->open_jid, sizeof(m->open_jid), jid ? jid : "");
    if (m->open_jid[0] && m->auth == AUTH_STATE_CONNECTED && !chat_jid_is_group(m->open_jid)) {
        m->deps.gateway->subscribe(m->deps.gateway, m->open_jid);
    }
    if (m->open_jid[0]) {
        Chat before;
        int unread = m->deps.chats->get(m->deps.chats, m->open_jid, &before) == 0 ? before.unread : 0;
        m->deps.chats->set_unread(m->deps.chats, m->open_jid, 0);
        clear_tally(m, m->open_jid);
        if (unread != 0) send_read(m, m->open_jid, unread < 0 ? 1 : unread);
        rebuild_chats(m);
    }
    reload_messages(m);
}

const char *messaging_manager_open_jid(MessagingManager *m) { return m->open_jid; }

int messaging_manager_history(MessagingManager *m, const char *jid, int64_t before, int limit, Message **out, int *count) {
    *out = NULL;
    *count = 0;
    if (!jid || !jid[0] || limit <= 0) return -1;
    int rc = before > 0 ? m->deps.messages->before(m->deps.messages, jid, before, limit, out, count)
                        : m->deps.messages->recent(m->deps.messages, jid, limit, out, count);
    if (rc != 0) return rc;
    for (int i = 0; i < *count; i++) {
        m->deps.reactions->summary(m->deps.reactions, (*out)[i].id, (*out)[i].reactions, sizeof((*out)[i].reactions));
    }
    return 0;
}

void messaging_manager_mark_read(MessagingManager *m, const char *jid) {
    if (!jid || !jid[0]) return;
    Chat before;
    if (m->deps.chats->get(m->deps.chats, jid, &before) != 0) return;
    m->deps.chats->set_unread(m->deps.chats, jid, 0);
    clear_tally(m, jid);
    send_read(m, jid, before.unread > 0 ? before.unread : 1);
    rebuild_chats(m);
}

int messaging_manager_live_since(MessagingManager *m, uint64_t after, LiveMessageRef *out, int max) {
    return live_message_ring_since(&m->live, after, out, max);
}

uint64_t messaging_manager_live_last(MessagingManager *m) { return live_message_ring_last(&m->live); }

void messaging_manager_note_scheduled_sent(MessagingManager *m, const char *scheduled_id, const char *chat_jid) {
    live_message_ring_note(&m->live, LIVE_KIND_SCHEDULED_SENT, scheduled_id, chat_jid, "", "", (int64_t)time(NULL));
}

const Message *messaging_manager_messages(MessagingManager *m, int *count) {
    *count = m->message_count;
    return m->messages;
}

/* Stores a message you send to `jid` as pending and moves its chat up. */
static int queue_outgoing(MessagingManager *m, Message *msg, const char *jid) {
    if (!jid || !jid[0]) return -1;
    if (!msg->id[0] && message_id_generate(msg->id, sizeof(msg->id)) != 0) return -1;
    str_copy(msg->chat_jid, sizeof(msg->chat_jid), jid);
    str_copy(msg->sender_jid, sizeof(msg->sender_jid), m->user_jid);
    msg->from_me = 1;
    msg->status = MESSAGE_STATUS_PENDING;
    msg->timestamp = (int64_t)time(NULL);
    m->deps.messages->save(m->deps.messages, msg);
    live_message_ring_push(&m->live, msg->id, jid);
    char preview[256];
    messaging_manager_message_preview(m, msg, preview, sizeof(preview));
    m->deps.chats->touch(m->deps.chats, jid, msg->timestamp, preview);
    m->chats_dirty = m->messages_dirty = 1;
    return 0;
}

int messaging_manager_send_text_to(MessagingManager *m, const char *jid, const OutgoingText *request) {
    if (!request || !request->text || !*request->text) return -1;
    /* Previews are fetched only when the user turned them on, and only for a message with a link. */
    OutgoingText with_preview = *request;
    char url[512];
    with_preview.want_link_preview = m->deps.settings->link_previews && url_find_first(request->text, url, sizeof(url)) == 0;
    const OutgoingText *out = &with_preview;
    if (jid && strcmp(jid, m->open_jid) == 0) messaging_manager_set_typing(m, TYPING_PAUSED);
    Message msg;
    message_init(&msg);
    msg.type = MESSAGE_TYPE_TEXT;
    message_set_text(&msg, out->text);
    msg.forwarded = out->forwarded;
    const QuoteRef *quote = out->quote;
    if (quote && quote->id[0]) {
        msg.quoted_status = quote->is_status;
        str_copy(msg.quoted_id, sizeof(msg.quoted_id), quote->id);
        str_copy(msg.quoted_sender, sizeof(msg.quoted_sender), quote->sender);
        message_set_quoted_text(&msg, quote->text);
    }
    char *mentions = mention_list_serialize(out->mentions);
    message_set_mentions(&msg, mentions);
    free(mentions);
    int rc = queue_outgoing(m, &msg, jid);
    if (rc == 0 && m->deps.gateway->send_text(m->deps.gateway, msg.chat_jid, out, msg.id) != 0) {
        m->deps.messages->update_status(m->deps.messages, msg.id, MESSAGE_STATUS_FAILED);
    }
    message_dispose(&msg);
    return rc;
}

int messaging_manager_send_text(MessagingManager *m, const char *text, const QuoteRef *quote) {
    OutgoingText out = { text, quote, NULL, 0, 0, 0 };
    return messaging_manager_send_text_to(m, m->open_jid, &out);
}

int messaging_manager_reply_to_status(MessagingManager *m, const StatusReplyTarget *status, const char *text) {
    if (!status || !status->status_id[0] || !status->author_jid[0] || !text || !*text) return -1;
    QuoteRef quote;
    memset(&quote, 0, sizeof(quote));
    str_copy(quote.id, sizeof(quote.id), status->status_id);
    str_copy(quote.sender, sizeof(quote.sender), status->author_jid);
    str_copy(quote.text, sizeof(quote.text), status->preview);
    quote.is_status = 1;
    OutgoingText out = { text, &quote, NULL, 0, 0, 0 };
    return messaging_manager_send_text_to(m, status->author_jid, &out);   /* in your chat with its author */
}

int messaging_manager_like_status(MessagingManager *m, const StatusReplyTarget *status) {
    static const char HEART[] = "\xE2\x9D\xA4\xEF\xB8\x8F";          /* ❤️ */
    if (!status || !status->status_id[0] || !status->author_jid[0]) return -1;
    if (m->deps.liker) return m->deps.liker->like(m->deps.liker, status->author_jid, status->status_id, HEART) == 0 ? 0 : -1;
    return messaging_manager_reply_to_status(m, status, HEART) == 0 ? 1 : -1;
}

int messaging_manager_send_text_mentioning(MessagingManager *m, const char *text, const QuoteRef *quote,
                                           const MentionPick *picks, int count) {
    if (!text || !*text) return -1;
    size_t size = strlen(text) + 64 * (size_t)(count > 0 ? count : 1) + 1;
    char *encoded = malloc(size);
    if (!encoded) return -1;
    MentionList mentions;
    int rc = -1;
    if (mention_encoder_encode(text, picks, count, encoded, size, &mentions) == 0) {
        OutgoingText out = { encoded, quote, mentions.count ? &mentions : NULL, 0, 0, 0 };
        rc = messaging_manager_send_text_to(m, m->open_jid, &out);
    }
    free(encoded);
    return rc;
}

int messaging_manager_rank_mentions(MessagingManager *m, const MentionCandidate *members, int count, const char *query,
                                    MentionCandidate *out, int max) {
    (void)m;
    return mention_matcher_rank(members, count, query, out, max);
}

int messaging_manager_send_voice(MessagingManager *m, const char *path, int seconds) {
    if (!path_is_within(path, m->deps.settings->media_dir)) return -1;
    Message msg;
    message_init(&msg);
    msg.type = MESSAGE_TYPE_AUDIO;
    msg.duration_s = seconds;
    str_copy(msg.media_path, sizeof(msg.media_path), path);
    int rc = queue_outgoing(m, &msg, m->open_jid);
    if (rc == 0 && m->deps.gateway->send_voice(m->deps.gateway, msg.chat_jid, path, seconds, msg.id) != 0) {
        m->deps.messages->update_status(m->deps.messages, msg.id, MESSAGE_STATUS_FAILED);
    }
    message_dispose(&msg);
    return rc;
}

int messaging_manager_send_file(MessagingManager *m, const char *path, const char *caption) {
    if (!m->open_jid[0] || !path_is_regular_file(path)) return -1;
    Message msg;
    message_init(&msg);
    msg.type = media_type_detect(path);
    if (caption && *caption) message_set_text(&msg, caption);
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    if (msg.type == MESSAGE_TYPE_DOCUMENT && !(caption && *caption)) message_set_text(&msg, name);

    char file[600];
    if (message_id_generate(msg.id, sizeof(msg.id)) != 0 ||
        outgoing_media_copy(m->deps.settings->media_dir, path, msg.id, file, sizeof(file)) != 0) {
        message_dispose(&msg);
        return -1;
    }
    str_copy(msg.media_path, sizeof(msg.media_path), file);

    char id[64];
    str_copy(id, sizeof(id), msg.id);
    int rc = queue_outgoing(m, &msg, m->open_jid);
    str_copy(msg.id, sizeof(msg.id), id);
    OutgoingMedia media = { file, message_type_name(msg.type), media_type_mime(path), name, caption, 0, 0 };
    if (rc == 0 && m->deps.gateway->send_media(m->deps.gateway, msg.chat_jid, &media, msg.id) != 0) {
        m->deps.messages->update_status(m->deps.messages, msg.id, MESSAGE_STATUS_FAILED);
    }
    message_dispose(&msg);
    return rc;
}

/* One copy of a photo, video, audio file or document to `jid`, marked as
 * forwarded. A downloaded file is uploaded again (which works however old
 * the original is); otherwise WhatsApp's own copy is sent on by reference. */
static int forward_media_to(MessagingManager *m, const Message *src, const char *jid, int score) {
    Message msg;
    message_init(&msg);
    msg.type = src->type;
    msg.forwarded = 1;
    msg.duration_s = src->duration_s;
    if (src->text) message_set_text(&msg, src->text);
    message_set_thumbnail(&msg, src->thumbnail, src->thumbnail_len);
    if (message_id_generate(msg.id, sizeof(msg.id)) != 0) { message_dispose(&msg); return -1; }
    int rc = -1;
    if (src->media_path[0] && path_is_regular_file(src->media_path)) {
        char file[600];
        if (outgoing_media_copy(m->deps.settings->media_dir, src->media_path, msg.id, file, sizeof(file)) == 0) {
            str_copy(msg.media_path, sizeof(msg.media_path), file);
            const char *slash = strrchr(src->media_path, '/');
            const char *name = slash ? slash + 1 : src->media_path;
            const char *caption = src->type == MESSAGE_TYPE_DOCUMENT ? "" : src->text;
            char id[64];
            str_copy(id, sizeof(id), msg.id);
            rc = queue_outgoing(m, &msg, jid);
            OutgoingMedia media = { file, message_type_name(src->type), media_type_mime(src->media_path), name, caption, 1, score };
            if (rc == 0 && m->deps.gateway->send_media(m->deps.gateway, jid, &media, id) != 0) {
                m->deps.messages->update_status(m->deps.messages, id, MESSAGE_STATUS_FAILED);
            }
        }
    } else if (src->media_ref) {
        message_set_media_ref(&msg, src->media_ref);
        rc = queue_outgoing(m, &msg, jid);
        if (rc == 0 && m->deps.gateway->forward_media(m->deps.gateway, jid, src->media_ref, score, msg.id) != 0) {
            m->deps.messages->update_status(m->deps.messages, msg.id, MESSAGE_STATUS_FAILED);
        }
    }
    message_dispose(&msg);
    return rc;
}

int messaging_manager_forward(MessagingManager *m, const char *message_id, const char *const *jids, int count) {
    Message src;
    if (!message_id || m->deps.messages->get(m->deps.messages, message_id, &src) != 0) return -1;
    int queued = 0;
    /* The score says how often a message has travelled; only whether it was
     * forwarded before is kept, which is enough for "Forwarded" and for
     * WhatsApp to count it on. */
    int score = src.forwarded ? 2 : 1;
    for (int i = 0; !src.deleted && i < count; i++) {
        if (!jids[i] || !jids[i][0]) continue;
        int rc;
        if (src.type == MESSAGE_TYPE_TEXT) {
            OutgoingText out = { src.text, NULL, NULL, 1, score, 0 };
            rc = messaging_manager_send_text_to(m, jids[i], &out);
        } else if (src.type == MESSAGE_TYPE_OTHER) {
            rc = -1;                                        /* polls, locations and the like cannot be sent on */
        } else {
            rc = forward_media_to(m, &src, jids[i], score);
        }
        if (rc == 0) queued++;
    }
    message_dispose(&src);
    return queued;
}

int messaging_manager_retry_message(MessagingManager *m, const char *id) {
    Message msg;
    if (m->deps.messages->get(m->deps.messages, id, &msg) != 0) return -1;
    int rc = -1;
    if (msg.from_me && msg.status == MESSAGE_STATUS_FAILED) {
        m->deps.messages->update_status(m->deps.messages, id, MESSAGE_STATUS_PENDING);
        if (msg.type == MESSAGE_TYPE_TEXT) {
            QuoteRef q;
            memset(&q, 0, sizeof(q));
            str_copy(q.id, sizeof(q.id), msg.quoted_id);
            str_copy(q.sender, sizeof(q.sender), msg.quoted_sender);
            str_copy(q.text, sizeof(q.text), msg.quoted_text);
            MentionList mentions;
            mention_list_parse(&mentions, msg.mentions);
            OutgoingText out = { msg.text ? msg.text : "", q.id[0] ? &q : NULL, &mentions, msg.forwarded, msg.forwarded, 0 };
            rc = m->deps.gateway->send_text(m->deps.gateway, msg.chat_jid, &out, msg.id);
        } else if (msg.type == MESSAGE_TYPE_AUDIO && msg.duration_s > 0) {
            rc = m->deps.gateway->send_voice(m->deps.gateway, msg.chat_jid, msg.media_path, msg.duration_s, msg.id);
        } else {
            const char *slash = strrchr(msg.media_path, '/');
            OutgoingMedia media = { msg.media_path, message_type_name(msg.type), media_type_mime(msg.media_path),
                                    slash ? slash + 1 : msg.media_path, msg.type == MESSAGE_TYPE_DOCUMENT ? "" : msg.text,
                                    msg.forwarded, msg.forwarded };
            rc = m->deps.gateway->send_media(m->deps.gateway, msg.chat_jid, &media, msg.id);
        }
        m->messages_dirty = 1;
    }
    message_dispose(&msg);
    return rc;
}

void messaging_manager_toggle_mute(MessagingManager *m, const char *jid) {
    Chat c;
    if (m->deps.chats->get(m->deps.chats, jid, &c) != 0) return;
    m->deps.chats->set_muted_until(m->deps.chats, jid, c.is_muted ? 0 : -1);
    rebuild_chats(m);
}

void messaging_manager_toggle_pin(MessagingManager *m, const char *jid) {
    Chat c;
    if (m->deps.chats->get(m->deps.chats, jid, &c) != 0) return;
    m->deps.chats->set_pinned(m->deps.chats, jid, !c.is_pinned);
    rebuild_chats(m);
}

static int download(MessagingManager *m, const char *id, int then_open) {
    Message msg;
    if (m->deps.messages->get(m->deps.messages, id, &msg) != 0) return -1;
    int rc = -1;
    if (msg.media_ref) {
        if (then_open) str_copy(m->pending_media_id, sizeof(m->pending_media_id), id);
        rc = m->deps.gateway->download_media(m->deps.gateway, id, msg.media_ref, 0);
    }
    message_dispose(&msg);
    return rc;
}

int messaging_manager_download(MessagingManager *m, const char *id) { return download(m, id, 1); }
int messaging_manager_fetch_media(MessagingManager *m, const char *id) { return download(m, id, 0); }

const UnreadTally *messaging_manager_tally(MessagingManager *m) { return &m->tally; }

AuthState   messaging_manager_auth_state(MessagingManager *m)   { return m->auth; }
const char *messaging_manager_qr(MessagingManager *m)           { return m->qr_ascii ? m->qr_ascii : ""; }
const char *messaging_manager_pairing_code(MessagingManager *m) { return m->pairing_code; }
const char *messaging_manager_user_name(MessagingManager *m)    { return m->user_name; }
const char *messaging_manager_user_jid(MessagingManager *m)     { return m->user_jid; }

void messaging_manager_request_pairing(MessagingManager *m, const char *phone) {
    m->pairing_code[0] = '\0';
    m->deps.gateway->request_pairing_code(m->deps.gateway, phone);
}

void messaging_manager_request_qr(MessagingManager *m) {
    m->pairing_code[0] = '\0';
    m->deps.gateway->request_qr(m->deps.gateway);
}

void messaging_manager_logout(MessagingManager *m) {
    m->deps.gateway->logout(m->deps.gateway);
}

void messaging_manager_health(MessagingManager *m, ConnectionHealth *out) {
    memset(out, 0, sizeof(*out));
    int64_t now = clock_now_ms();
    out->available = m->auth == AUTH_STATE_CONNECTED;
    out->attempt = m->attempt;
    out->breaker = m->breaker.state;
    out->will_retry = m->pending != PENDING_NONE && !m->retry_blocked;
    out->retry_in_ms = out->will_retry ? (m->pending_at_ms > now ? m->pending_at_ms - now : 0) : 0;
    if (m->breaker.state == CIRCUIT_OPEN) {
        int64_t wait = circuit_breaker_remaining_ms(&m->breaker, now);
        if (wait > out->retry_in_ms) out->retry_in_ms = wait;
    }
    out->show_overlay = !out->available && m->auth != AUTH_STATE_NEEDS_LOGIN &&
                        (m->attempt > 0 || m->retry_blocked || m->breaker.state != CIRCUIT_CLOSED);
    str_copy(out->title, sizeof(out->title), m->outage_title[0] ? m->outage_title : "Connecting");
    str_copy(out->detail, sizeof(out->detail), m->outage_detail);
}

int messaging_manager_react(MessagingManager *m, const char *message_id, const char *emoji) {
    Message msg;
    if (m->deps.messages->get(m->deps.messages, message_id, &msg) != 0) return -1;
    ReactionTarget t;
    memset(&t, 0, sizeof(t));
    str_copy(t.chat, sizeof(t.chat), msg.chat_jid);
    str_copy(t.id, sizeof(t.id), msg.id);
    str_copy(t.sender, sizeof(t.sender), msg.from_me ? m->user_jid : msg.sender_jid);
    t.from_me = msg.from_me;
    message_dispose(&msg);
    const char *me = m->user_jid[0] ? m->user_jid : "me";
    m->deps.reactions->put(m->deps.reactions, message_id, me, emoji);
    m->messages_dirty = 1;
    return m->deps.gateway->react(m->deps.gateway, &t, emoji);
}

void messaging_manager_set_typing(MessagingManager *m, TypingState state) {
    if (!m->deps.settings->share_typing || m->auth != AUTH_STATE_CONNECTED) return;
    int64_t now = clock_now_ms();
    if (state == TYPING_PAUSED) {
        if (m->my_typing != TYPING_PAUSED && m->my_typing_chat[0]) {
            m->deps.gateway->typing(m->deps.gateway, m->my_typing_chat, "paused");
        }
        m->my_typing = TYPING_PAUSED;
        return;
    }
    if (!m->open_jid[0]) return;
    int same = m->my_typing == state && strcmp(m->my_typing_chat, m->open_jid) == 0;
    if (same && now - m->my_typing_sent_ms < TYPING_RESEND_MS) return;
    str_copy(m->my_typing_chat, sizeof(m->my_typing_chat), m->open_jid);
    m->my_typing = state;
    m->my_typing_sent_ms = now;
    m->deps.gateway->typing(m->deps.gateway, m->open_jid, typing_state_name(state));
}

void messaging_manager_set_active(MessagingManager *m, int active) {
    m->active = active ? 1 : 0;
    if (!active) messaging_manager_set_typing(m, TYPING_PAUSED);
    send_presence(m);
}

void messaging_manager_save_draft(MessagingManager *m, const char *jid, const char *text) {
    if (!jid || !jid[0]) return;
    char *old = m->deps.chats->get_draft(m->deps.chats, jid);
    int changed = strcmp(old ? old : "", text ? text : "") != 0;
    free(old);
    if (!changed) return;
    m->deps.chats->set_draft(m->deps.chats, jid, text ? text : "");
    m->chats_dirty = 1;
}

char *messaging_manager_load_draft(MessagingManager *m, const char *jid) {
    return m->deps.chats->get_draft(m->deps.chats, jid);
}

int messaging_manager_offer_draft(MessagingManager *m, const char *jid, const char *text) {
    if (!jid || !jid[0] || !text || !*text || m->offered_text) return -1;
    int open = strcmp(m->open_jid, jid) == 0;              /* the input box holds that chat's draft */
    if (!open) {
        char *old = m->deps.chats->get_draft(m->deps.chats, jid);
        int exists = old && *old;
        free(old);
        if (exists) return 1;
        m->deps.chats->set_draft(m->deps.chats, jid, text);
        m->chats_dirty = 1;
    }
    str_copy(m->offered_jid, sizeof(m->offered_jid), jid);
    m->offered_text = str_dup(text);
    return 0;
}

char *messaging_manager_take_offered_draft(MessagingManager *m, char *jid_out, size_t size) {
    if (!m->offered_text) return NULL;
    str_copy(jid_out, size, m->offered_jid);
    char *text = m->offered_text;
    m->offered_text = NULL;
    m->offered_jid[0] = '\0';
    return text;
}

static int chat_soft_locked(const MessagingManager *m, const char *jid) {
    for (int i = 0; i < m->chat_count; i++) if (strcmp(m->chats[i].jid, jid) == 0) return m->chats[i].soft_locked;
    return 0;
}

/* Soft-locked chats stay hidden in search results too. */
int messaging_manager_search(MessagingManager *m, const char *query, int limit, Message **out, int *count) {
    int rc = m->deps.messages->search(m->deps.messages, query, limit, out, count);
    if (rc != 0 || !*out) return rc;
    int kept = 0;
    for (int i = 0; i < *count; i++) {
        if (chat_soft_locked(m, (*out)[i].chat_jid)) { message_dispose(&(*out)[i]); continue; }
        if (kept != i) (*out)[kept] = (*out)[i];
        kept++;
    }
    *count = kept;
    return 0;
}

void messaging_manager_mute_until(MessagingManager *m, const char *jid, int64_t until) {
    m->deps.chats->set_muted_until(m->deps.chats, jid, until);
    rebuild_chats(m);
}

void messaging_manager_set_tone(MessagingManager *m, const char *jid, const char *tone) {
    m->deps.chats->set_tone(m->deps.chats, jid, tone);
    rebuild_chats(m);
}

/* Forgets a chat locally: messages, reactions, the chat row, its unread tally. */
static void forget_chat(MessagingManager *m, const char *jid, ManagerChanges *ch) {
    m->deps.messages->remove_chat(m->deps.messages, jid);
    m->deps.chats->remove(m->deps.chats, jid);
    clear_tally(m, jid);
    if (strcmp(m->open_jid, jid) == 0) {
        m->open_jid[0] = '\0';
        m->messages_dirty = 1;
    }
    m->chats_dirty = 1;
    if (ch) ch->chats = ch->messages = 1;
}

int messaging_manager_delete_chat(MessagingManager *m, const char *jid) {
    if (!jid || !jid[0]) return -1;
    char chat[128];
    str_copy(chat, sizeof(chat), jid);
    DeleteRequest req;
    memset(&req, 0, sizeof(req));
    str_copy(req.chat, sizeof(req.chat), chat);
    Message *newest = NULL;
    int n = 0;
    if (m->deps.messages->recent(m->deps.messages, chat, 1, &newest, &n) == 0 && n > 0) {
        str_copy(req.id, sizeof(req.id), newest[0].id);
        req.from_me = newest[0].from_me;
        req.timestamp = newest[0].timestamp;
    }
    message_array_free(newest, n);
    int rc = m->deps.gateway->delete_chat(m->deps.gateway, &req);
    forget_chat(m, chat, NULL);
    rebuild_chats(m);
    return rc;
}

int messaging_manager_clear_chat(MessagingManager *m, const char *jid) {
    if (!jid || !jid[0]) return -1;
    int rc = m->deps.messages->remove_chat(m->deps.messages, jid);
    for (int i = 0; i < m->chat_count; i++) {
        if (strcmp(m->chats[i].jid, jid) != 0) continue;
        Chat c = m->chats[i];
        c.preview[0] = '\0';
        c.unread = 0;
        m->deps.chats->upsert(m->deps.chats, &c);
    }
    clear_tally(m, jid);
    if (strcmp(m->open_jid, jid) == 0) m->messages_dirty = 1;
    m->chats_dirty = 1;
    return rc;
}

static const char *export_sender(void *ctx, const Message *msg) {
    static char name[128];
    MessagingManager *m = ctx;
    if (msg->from_me) return "You";
    if (msg->sender_name[0]) return msg->sender_name;
    messaging_manager_display_name(m, msg->sender_jid[0] ? msg->sender_jid : msg->chat_jid, name, sizeof(name));
    return name;
}

int messaging_manager_export_chat(MessagingManager *m, const char *jid, const char *dir, int with_media, char *out, size_t size) {
    if (!m->deps.exporter || !jid || !jid[0]) return -1;
    Message *all = NULL;
    int n = 0;
    if (m->deps.messages->recent(m->deps.messages, jid, 1000000, &all, &n) != 0) return -1;
    char name[128] = "chat";
    for (int i = 0; i < m->chat_count; i++) if (strcmp(m->chats[i].jid, jid) == 0) str_copy(name, sizeof(name), m->chats[i].name);
    int rc = m->deps.exporter->export_chat(m->deps.exporter, name, all, n, export_sender, m, dir, with_media, out, size);
    message_array_free(all, n);
    return rc;
}

int messaging_manager_toggle_soft_lock(MessagingManager *m, const char *jid) {
    int locked = 0;
    for (int i = 0; i < m->chat_count; i++) if (strcmp(m->chats[i].jid, jid) == 0) locked = m->chats[i].soft_locked;
    m->deps.chats->set_soft_locked(m->deps.chats, jid, !locked);
    rebuild_chats(m);
    return !locked;
}

void messaging_manager_set_archived(MessagingManager *m, const char *jid, int archived) {
    m->deps.chats->set_archived(m->deps.chats, jid, archived);
    rebuild_chats(m);
}

void messaging_manager_set_chat_theme(MessagingManager *m, const char *jid, const char *theme_id) {
    m->deps.chats->set_theme(m->deps.chats, jid, theme_id ? theme_id : "");
    rebuild_chats(m);
}

const Chat *messaging_manager_open_chat_info(MessagingManager *m) {
    for (int i = 0; m->open_jid[0] && i < m->chat_count; i++) {
        if (strcmp(m->chats[i].jid, m->open_jid) == 0) return &m->chats[i];
    }
    return NULL;
}

#define PHONE_HISTORY_BATCH  50
#define PHONE_HISTORY_WAIT_MS 20000

int messaging_manager_load_older(MessagingManager *m) {
    if (!m->open_jid[0] || m->message_count == 0) return 0;
    int before = m->message_count;
    if (m->message_count >= m->window) {           /* the database may have more */
        m->window += window_margin(m);
        reload_messages(m);
        if (m->message_count > before) return 1;
    }
    /* Nothing older locally: ask the phone, once at a time. */
    if (m->history_until_ms || m->auth != AUTH_STATE_CONNECTED) return 0;
    const Message *oldest = &m->messages[0];
    HistoryAnchor anchor;
    memset(&anchor, 0, sizeof(anchor));
    str_copy(anchor.chat, sizeof(anchor.chat), m->open_jid);
    str_copy(anchor.id, sizeof(anchor.id), oldest->id);
    anchor.timestamp = oldest->timestamp;
    anchor.from_me = oldest->from_me;
    m->history_count_before = m->message_count;
    m->window = m->message_count + PHONE_HISTORY_BATCH;
    m->history_until_ms = clock_now_ms() + PHONE_HISTORY_WAIT_MS;
    m->deps.gateway->request_older(m->deps.gateway, &anchor, PHONE_HISTORY_BATCH);
    return 0;
}

int messaging_manager_history_pending(MessagingManager *m) { return m->history_until_ms != 0; }

/* Slides the loaded window so that about a margin of messages is kept either
 * side of the ones on screen: more are loaded where a side runs short, and the
 * surplus on the other side is let go. Half a margin of slack keeps a scroll of
 * a few rows from reloading every time. */
int messaging_manager_focus_window(MessagingManager *m, int first_visible, int last_visible) {
    if (!m->open_jid[0] || m->message_count == 0 || m->history_until_ms) return 0;
    if (first_visible < 0 || last_visible < first_visible || last_visible >= m->message_count) return 0;
    int margin = window_margin(m), slack = margin / 2;
    int older = first_visible;
    int newer = m->message_count - 1 - last_visible;
    int may_have_older = m->message_count >= m->window;
    int short_of = (newer < margin - slack && m->skip > 0) || (older < margin - slack && may_have_older);
    int surplus = newer > margin + slack || older > margin + slack;
    if (!short_of && !surplus) return 0;
    int skip = m->skip + newer - margin;                    /* counted from the newest message of the chat */
    if (skip < 0) skip = 0;
    int window = m->skip + (m->message_count - 1 - first_visible) + margin - skip + 1;
    if (skip == m->skip && window == m->window) return 0;
    m->skip = skip;
    m->window = window;
    reload_messages(m);
    return 1;
}

int messaging_manager_show_latest(MessagingManager *m) {
    if (m->skip == 0) return 0;
    m->skip = 0;
    m->window = window_margin(m) + window_margin(m) / 2;
    reload_messages(m);
    return 1;
}

int messaging_manager_has_newer(MessagingManager *m) { return m->skip > 0; }

#define EDIT_WINDOW_SECONDS (15 * 60)

int messaging_manager_can_edit(MessagingManager *m, const Message *msg) {
    (void)m;
    return msg && msg->from_me && msg->type == MESSAGE_TYPE_TEXT && !msg->deleted &&
           msg->status != MESSAGE_STATUS_FAILED && (int64_t)time(NULL) - msg->timestamp < EDIT_WINDOW_SECONDS;
}

#define REVOKE_WINDOW_SECONDS (60 * 60 * 60)     /* WhatsApp allows about two and a half days */

int messaging_manager_can_delete_for_everyone(MessagingManager *m, const Message *msg) {
    (void)m;
    return msg && msg->from_me && !msg->deleted && msg->status != MESSAGE_STATUS_FAILED &&
           msg->status != MESSAGE_STATUS_PENDING && (int64_t)time(NULL) - msg->timestamp < REVOKE_WINDOW_SECONDS;
}

int messaging_manager_delete(MessagingManager *m, const char *message_id, int for_everyone) {
    Message msg;
    if (m->deps.messages->get(m->deps.messages, message_id, &msg) != 0) return -1;
    if (for_everyone && !messaging_manager_can_delete_for_everyone(m, &msg)) { message_dispose(&msg); return -1; }
    DeleteRequest req;
    memset(&req, 0, sizeof(req));
    str_copy(req.chat, sizeof(req.chat), msg.chat_jid);
    str_copy(req.id, sizeof(req.id), msg.id);
    if (!msg.from_me) str_copy(req.sender, sizeof(req.sender), msg.sender_jid);
    req.from_me = msg.from_me;
    req.timestamp = msg.timestamp;
    req.everyone = for_everyone;
    /* A failed or unsent message never reached WhatsApp: only remove it here. */
    int local_only = msg.from_me && (msg.status == MESSAGE_STATUS_FAILED || msg.status == MESSAGE_STATUS_PENDING);
    message_dispose(&msg);
    int rc = local_only ? 0 : m->deps.gateway->delete_message(m->deps.gateway, &req);
    if (for_everyone) m->deps.messages->edit_text(m->deps.messages, message_id, NULL, 1);
    else m->deps.messages->remove(m->deps.messages, message_id);
    m->messages_dirty = 1;
    m->chats_dirty = 1;
    return rc;
}

int messaging_manager_get(MessagingManager *m, const char *message_id, Message *out) {
    return m->deps.messages->get(m->deps.messages, message_id, out);
}

int messaging_manager_edit(MessagingManager *m, const char *message_id, const char *text) {
    if (!text || !*text) return -1;
    Message msg;
    if (m->deps.messages->get(m->deps.messages, message_id, &msg) != 0) return -1;
    int ok = messaging_manager_can_edit(m, &msg);
    char chat[128];
    str_copy(chat, sizeof(chat), msg.chat_jid);
    message_dispose(&msg);
    if (!ok) return -1;
    m->deps.messages->edit_text(m->deps.messages, message_id, text, 0);
    m->messages_dirty = 1;
    return m->deps.gateway->edit(m->deps.gateway, chat, message_id, text);
}
