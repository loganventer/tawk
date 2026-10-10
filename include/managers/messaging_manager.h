#ifndef APP_MANAGERS_MESSAGING_MANAGER_H
#define APP_MANAGERS_MESSAGING_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include "core/receipt.h"
#include "core/auth_state.h"
#include "core/chat.h"
#include "core/contact_presence.h"
#include "core/live_message_ref.h"
#include "core/mention_candidate.h"
#include "core/mention_pick.h"
#include "core/message.h"
#include "core/outgoing_text.h"
#include "core/quote_ref.h"
#include "core/status_reply_target.h"
#include "core/styled_text.h"
#include "core/typing_state.h"
#include "core/unread_tally.h"
#include "managers/connection_health.h"
#include "managers/manager_changes.h"
#include "managers/messaging_manager_deps.h"

/* Use cases for chatting: syncing, reading, sending, login and connection
 * supervision. Borrows every dependency. Single-threaded (UI thread). */
typedef struct MessagingManager MessagingManager;

MessagingManager *messaging_manager_create(const MessagingManagerDeps *deps);
void              messaging_manager_destroy(MessagingManager *mgr);

void messaging_manager_start(MessagingManager *mgr);
/* Drains events and runs reconnect timers. */
void messaging_manager_tick(MessagingManager *mgr, ManagerChanges *changes);
/* Skips the backoff wait (the overlay's "retry now"). */
void messaging_manager_retry_now(MessagingManager *mgr);

/* Chats and messages */
const Chat    *messaging_manager_chats(MessagingManager *mgr, int *count);
void           messaging_manager_open_chat(MessagingManager *mgr, const char *jid);
const char    *messaging_manager_open_jid(MessagingManager *mgr);
const Message *messaging_manager_messages(MessagingManager *mgr, int *count);
/* Any chat's messages without opening it or marking anything read: the
 * newest `limit`, or those before `before` (epoch seconds) when it is not 0,
 * oldest first with their reactions. Free with message_array_free. */
int            messaging_manager_history(MessagingManager *mgr, const char *jid, int64_t before, int limit,
                                         Message **out, int *count);
/* Marks a chat read (here, and on WhatsApp as opening it would) without opening it. */
void           messaging_manager_mark_read(MessagingManager *mgr, const char *jid);
/* Messages that arrived or were sent after sequence number `after`, oldest
 * first; returns how many were written. */
int            messaging_manager_live_since(MessagingManager *mgr, uint64_t after, LiveMessageRef *out, int max);
uint64_t       messaging_manager_live_last(MessagingManager *mgr);
/* A message you scheduled went out: those who follow along hear of it, by the scheduled message's id. */
void           messaging_manager_note_scheduled_sent(MessagingManager *mgr, const char *scheduled_id, const char *chat_jid);
/* quote may be NULL; otherwise the message is a reply to it. */
int            messaging_manager_send_text(MessagingManager *mgr, const char *text, const QuoteRef *quote);
/* Sends text with the people picked while typing it mentioned: their
 * "@<name>" becomes the "@<number>" WhatsApp sends. */
int            messaging_manager_send_text_mentioning(MessagingManager *mgr, const char *text, const QuoteRef *quote,
                                                      const MentionPick *picks, int count);
/* Replies to someone's status in your chat with its author, quoting it. */
int            messaging_manager_reply_to_status(MessagingManager *mgr, const StatusReplyTarget *status, const char *text);
/* Likes someone's status: privately where the backend can address the author
 * alone (returns 0), else as a ❤️ reply in your chat with them (returns 1). -1 on failure. */
int            messaging_manager_like_status(MessagingManager *mgr, const StatusReplyTarget *status);
/* The members that fit an "@name" being typed, best first. */
int            messaging_manager_rank_mentions(MessagingManager *mgr, const MentionCandidate *members, int count,
                                               const char *query, MentionCandidate *out, int max);
/* A message's text as shown, formatting marks taken out and mentions named;
 * -1 when it is shown as typed (formatting off, no text). The caller disposes `out`. */
int            messaging_manager_format_message(MessagingManager *mgr, const Message *msg, StyledText *out);
/* One line for chat lists, search results and replies, without formatting marks. */
void           messaging_manager_message_preview(MessagingManager *mgr, const Message *msg, char *out, size_t size);
/* Sends text to any chat (not only the open one), with mentions, forwarding or a link preview. */
int            messaging_manager_send_text_to(MessagingManager *mgr, const char *jid, const OutgoingText *text);
/* Edits one of our own text messages (WhatsApp allows 15 minutes). Returns
 * 0, or -1 when the message cannot be edited. */
int            messaging_manager_edit(MessagingManager *mgr, const char *message_id, const char *text);
/* A copy of one stored message; free it with message_dispose. */
int            messaging_manager_get(MessagingManager *mgr, const char *message_id, Message *out);
/* True when the message is ours, text, not deleted and young enough to edit. */
int            messaging_manager_can_edit(MessagingManager *mgr, const Message *msg);
/* Delete for everyone: your own messages, within WhatsApp's time limit. */
int            messaging_manager_can_delete_for_everyone(MessagingManager *mgr, const Message *msg);
/* Deletes a message for everyone, or only here and on your other devices. */
int            messaging_manager_delete(MessagingManager *mgr, const char *message_id, int for_everyone);
/* Reacts to a message in the open chat; an empty emoji removes our reaction. */
int            messaging_manager_react(MessagingManager *mgr, const char *message_id, const char *emoji);
/* Our typing state in the open chat; throttled before it reaches WhatsApp. */
void           messaging_manager_set_typing(MessagingManager *mgr, TypingState state);
/* Active while you use tawk; inactive when idle or in the screensaver. */
void           messaging_manager_set_active(MessagingManager *mgr, int active);
/* What is known about `jid` being online: 1 and `out` filled, or 0 when nothing is known. */
int            messaging_manager_presence(MessagingManager *mgr, const char *jid, ContactPresence *out);
/* Asks WhatsApp to tell us when `jid` comes online or leaves, as opening their chat does.
 * Returns 0 when it cannot: a group, or we are not connected and shown as online ourselves. */
int            messaging_manager_watch_presence(MessagingManager *mgr, const char *jid);
/* Drafts are kept per chat in the database. */
void           messaging_manager_save_draft(MessagingManager *mgr, const char *jid, const char *text);
char          *messaging_manager_load_draft(MessagingManager *mgr, const char *jid);
/* A draft another client wrote for `jid`, to edit and send yourself. A
 * chat that already has a draft keeps it (returns 1); the open chat's is
 * in the input box, where the screen adds this one. */
int            messaging_manager_offer_draft(MessagingManager *mgr, const char *jid, const char *text);
/* Hands the screen the last offered draft once; the caller frees it. */
char          *messaging_manager_take_offered_draft(MessagingManager *mgr, char *jid_out, size_t size);
/* Full-text search across chats, newest first. Caller frees with message_array_free. */
int            messaging_manager_search(MessagingManager *mgr, const char *query, int limit, Message **out, int *count);
/* Loads older messages of the open chat: first from the database; when it
 * has none, asks the phone. Returns 1 when more messages were loaded now. */
int            messaging_manager_load_older(MessagingManager *mgr);
/* True while older history has been requested from the phone. */
int            messaging_manager_history_pending(MessagingManager *mgr);
/* Tells the manager which loaded messages are on screen (indexes into
 * messaging_manager_messages). It keeps about a margin of messages either side
 * of them, loading and letting go as the user scrolls. Returns 1 when the
 * loaded messages changed. */
int            messaging_manager_focus_window(MessagingManager *mgr, int first_visible, int last_visible);
/* Goes back to the newest messages. Returns 1 when the loaded messages changed. */
int            messaging_manager_show_latest(MessagingManager *mgr);
/* True when messages newer than the loaded ones exist. */
int            messaging_manager_has_newer(MessagingManager *mgr);
/* Chat options */
void           messaging_manager_mute_until(MessagingManager *mgr, const char *jid, int64_t until);
void           messaging_manager_set_tone(MessagingManager *mgr, const char *jid, const char *tone);
void           messaging_manager_set_archived(MessagingManager *mgr, const char *jid, int archived);
/* Soft-locks a chat (its conversation is blurred) or unlocks it; returns the new state. */
int            messaging_manager_toggle_soft_lock(MessagingManager *mgr, const char *jid);
/* Deletes a whole chat here and on your other devices and phone. Permanent. */
int            messaging_manager_delete_chat(MessagingManager *mgr, const char *jid);
/* Deletes the chat's messages on this computer, keeping the chat. */
int            messaging_manager_clear_chat(MessagingManager *mgr, const char *jid);
/* Exports the whole chat into `dir` (with the downloaded files when asked);
 * the created file or folder goes to `out`. */
int            messaging_manager_export_chat(MessagingManager *mgr, const char *jid, const char *dir, int with_media,
                                             char *out, size_t size);
void           messaging_manager_set_chat_theme(MessagingManager *mgr, const char *jid, const char *theme_id);
/* The open chat, or NULL when none is open. */
const Chat    *messaging_manager_open_chat_info(MessagingManager *mgr);
int            messaging_manager_send_voice(MessagingManager *mgr, const char *path, int seconds);
/* Copies a local file into the media folder and sends it with an optional caption. */
int            messaging_manager_send_file(MessagingManager *mgr, const char *path, const char *caption);
int            messaging_manager_retry_message(MessagingManager *mgr, const char *message_id);
/* Sends a copy of a message to each chat in `jids`, marked as forwarded.
 * Returns how many copies were queued (-1 when the message is unknown). */
int            messaging_manager_forward(MessagingManager *mgr, const char *message_id, const char *const *jids, int count);
void           messaging_manager_toggle_mute(MessagingManager *mgr, const char *jid);
void           messaging_manager_toggle_pin(MessagingManager *mgr, const char *jid);
/* Starts a download; the result arrives as ManagerChanges.media_*. */
int            messaging_manager_download(MessagingManager *mgr, const char *message_id);
/* The same without opening it when it arrives (for other clients). */
int            messaging_manager_fetch_media(MessagingManager *mgr, const char *message_id);
const UnreadTally *messaging_manager_tally(MessagingManager *mgr);
/* Display name for a JID (contact, push name, or +number). */
void           messaging_manager_display_name(MessagingManager *mgr, const char *jid, char *out, unsigned long size);
/* The contacts `ref` could mean, for finding someone who has no chat yet:
 * the one with that phone number or JID when it is one, else those whose
 * name holds `ref`. Up to `max`; returns how many. */
int            messaging_manager_find_contacts(MessagingManager *mgr, const char *ref, Contact *out, int max);
/* Who received, read and played a message you sent, with their names; returns how many. */
int            messaging_manager_message_receipts(MessagingManager *mgr, const char *message_id, Receipt *out, int max);

/* Login */
AuthState   messaging_manager_auth_state(MessagingManager *mgr);
const char *messaging_manager_qr(MessagingManager *mgr);
const char *messaging_manager_pairing_code(MessagingManager *mgr);
const char *messaging_manager_user_name(MessagingManager *mgr);
const char *messaging_manager_user_jid(MessagingManager *mgr);
/* Which of a chat's messages alert you, and setting it. It holds for that person or group on all your numbers. */
ChatAlertLevel messaging_manager_alert_level(MessagingManager *mgr, const char *jid);
int            messaging_manager_set_alert_level(MessagingManager *mgr, const char *jid, ChatAlertLevel level);
void        messaging_manager_request_pairing(MessagingManager *mgr, const char *phone);
void        messaging_manager_request_qr(MessagingManager *mgr);
void        messaging_manager_logout(MessagingManager *mgr);

void messaging_manager_health(MessagingManager *mgr, ConnectionHealth *out);

#endif
