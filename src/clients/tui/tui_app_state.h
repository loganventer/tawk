#ifndef APP_CLIENTS_TUI_TUI_APP_STATE_H
#define APP_CLIENTS_TUI_TUI_APP_STATE_H

/* Private to the tui_*.c files that make up the terminal client. */

#include <stddef.h>
#include <stdint.h>

#include "clients/tui/chat_list_view.h"
#include "clients/tui/unified_chat_list.h"
#include "clients/tui/accounts_dialog.h"
#include "clients/tui/send_account_dialog.h"
#include "clients/tui/merged_message_window.h"
#include "clients/tui/chat_picker.h"
#include "clients/tui/chat_toggle_dialog.h"
#include "clients/tui/agents_panel.h"
#include "clients/tui/attach_menu.h"
#include "clients/tui/camera_purpose.h"
#include "clients/tui/camera_view.h"
#include "clients/tui/message_info_panel.h"
#include "clients/tui/chat_options_menu.h"
#include "clients/tui/command_suggestions.h"
#include "clients/tui/emoji_suggestions.h"
#include "clients/tui/composer_view.h"
#include "clients/tui/confirm_dialog.h"
#include "clients/tui/contact_panel.h"
#include "clients/tui/incoming_call_view.h"
#include "clients/tui/emoji_picker.h"
#include "clients/tui/file_picker.h"
#include "clients/tui/file_picker_purpose.h"
#include "clients/tui/header_hits.h"
#include "clients/tui/image_viewer.h"
#include "clients/tui/login_view.h"
#include "clients/tui/portrait_source.h"
#include "clients/tui/profile_dialogs.h"
#include "clients/tui/message_menu.h"
#include "clients/tui/mention_suggestions.h"
#include "core/mention_list.h"
#include "core/mention_pick.h"
#include "clients/tui/message_view.h"
#include "clients/tui/reaction_palette.h"
#include "clients/tui/scheduled_list_dialog.h"
#include "clients/tui/search_overlay.h"
#include "clients/tui/settings_panel.h"
#include "clients/tui/sixel_image_cache.h"
#include "clients/tui/sixel_overlay.h"
#include "clients/tui/splash_view.h"
#include "clients/tui/slash_command.h"
#include "clients/tui/status_composer_dialog.h"
#include "clients/tui/status_feed_dialogs.h"
#include "clients/tui/text_reader.h"
#include "clients/tui/theme_picker_overlay.h"
#include "clients/tui/thumbnail_cache.h"
#include "clients/tui/tui_app.h"
#include "clients/tui/tui_focus.h"
#include "clients/tui/tui_layout.h"
#include "core/quote_ref.h"
#include "engines/idle_tracker.h"

struct TuiApp {
    TuiAppDeps          deps;
    TuiLayout           layout;
    TuiFocus            focus;

    /* Views and popups */
    ChatListView        chat_list;
    MessageView         message_view;
    ComposerView        composer;
    LoginView           login;
    SettingsPanel       settings_panel;
    FilePicker          file_picker;
    FilePickerPurpose   picker_purpose;
    char                tone_jid[128];
    SearchOverlay       search;
    ReactionPalette     palette;
    EmojiPicker         emoji_picker;
    MessageMenu         message_menu;
    ChatOptionsMenu     options;
    AttachMenu          attach_menu;
    CameraView          camera_view;
    CameraPurpose       camera_purpose;
    MessageInfoPanel    message_info;
    ThemePickerOverlay  theme_picker;
    TextReader          reader;
    ImageViewer         viewer;
    ConfirmDialog       confirm;              /* yes-or-no questions, above everything else */
    ContactPanel        contact;              /* contact or group details */
    IncomingCallView    call_view;            /* the ringing call */
    ProfileDialogs      profile;              /* your own profile: view, name and about editor, photo menu */
    StatusComposerDialog status_composer;     /* writing a status */
    StatusFeedDialogs   feed;                 /* looking at statuses */
    MessageFormatter    formatter;            /* formatted message text, from the messaging manager */
    StatusSource        status_source;        /* the statuses replies answer, from the status feed manager */
    TranscriptSource    transcript_source;    /* the transcripts of voice notes, from the transcript manager */
    SummarySource       summary_source;       /* the TL;DR summaries of long messages, from the summary manager */
    HeaderHits          header_hits;          /* where the header drew + and your name */
    SplashView          splash;               /* the start-up animation */
    ChatPicker          forward_picker;       /* choosing chats to forward a message to */
    ChatToggleDialog    self_chats;           /* the chats an admin agent may answer its own requests in */
    ChatToggleDialog    voice_languages;      /* the languages one chat's voice notes are spoken in: the same list of switches */
    char                voice_languages_jid[128];  /* the chat it was opened for */
    SendAccountDialog   send_accounts;        /* the contacts with a sending account of their own */
    AccountsDialog      accounts_dialog;      /* your accounts: add, name, link and remove them */
    UnreadTally         total_tally;          /* new notifications over every account, for the header and the title */
    MergedMessageWindow merged;               /* the open chat across the accounts that share it */
    AccountId           peers[ACCOUNT_MAX];   /* the other accounts the open chat is merged with */
    int                 peer_count;
    UnifiedChatList     chat_rows;            /* every running account's chats as one list, when there is more than one */
    int                 chat_rows_stale;      /* an account's chats changed: build the list again */
    int64_t             chat_rows_built_ms;
    AccountId           account_filter;       /* the one account the chat list shows; ACCOUNT_ID_NONE: all of them */
    int                 self_chats_from_agents; /* opened from the Agents tab, so close back to it */
    char                forward_id[64];       /* the message being forwarded */
    ScheduledListDialog scheduled_list;       /* messages waiting to be sent later */
    AgentsPanel         agents;               /* the Agents tab */
    uint64_t            agents_seen;          /* requests already told about */
    int                 agents_notice;        /* a new request to mention once you stop typing */
    int64_t             agents_approvals[16]; /* when you last approved, to notice rubber-stamping */
    int64_t             last_key_ms;          /* so nothing is announced while you type */
    unsigned            settings_revision;    /* the settings as last followed */
    char                followed_theme[32];   /* the app theme and mouse setting as last applied */
    int                 followed_mouse;
    Settings            pending_settings;     /* waiting for you to confirm turning on agent access */
    int64_t             last_ring_ms;
    MediaSources        media_sources;        /* video frames and PDF pages, from deps */
    PortraitSource      portraits;            /* profile pictures, from the profile manager */
    char                open_outside_id[64];  /* photo to open in the system viewer once downloaded */
    char                save_after_id[64];    /* file to save to Downloads once downloaded */
    int                 unknown_offer;        /* the save-or-open offer was shown; Open goes ahead */
    CommandSuggestions  suggestions;
    EmojiSuggestions    emoji_suggestions;   /* choices for a "(word)" shortcode */
    MentionSuggestions  mention_suggestions; /* group members for an "@name" being typed */
    MentionPick         mention_picks[MENTION_LIST_MAX];   /* people mentioned in the input so far */
    int                 mention_pick_count;
    ThumbnailCache     *thumbs;
    SixelImageCache    *sixels;
    SixelOverlay        sixel_overlay;

    /* Composer state */
    char                attachment[1024];     /* dropped or picked file waiting to be sent */
    char                last_attach_dir[1024];
    int                 restore_pending;     /* reopen last_chat once the chat list is loaded */
    QuoteRef            reply;                /* reply.id[0] while replying */
    char                editing_id[64];       /* message being edited, or "" */
    char                reply_name[64];

    /* Conversation theme currently applied ("" = app theme) */
    char                conversation_theme[48];
    int                 conversation_theme_valid;

    /* Loop, idle and presence */
    IdleTracker         idle;
    int                 running;
    int                 active;
    int                 dragging_divider;
    int                 drag_width;
    int                 run_screensaver_now;
    int                 caret_visible;
    int                 dirty;
    int64_t             last_frame_ms;
    int64_t             last_minute;
    AuthState           last_auth;
    int64_t             linked_until_ms;

    char                toast[256];
    int                 toast_error;
    int64_t             toast_until_ms;
};

/* tui_app.c */
void tui_app_toast(TuiApp *app, const char *text, int is_error);
void tui_app_invalidate(TuiApp *app);
int  tui_app_show_login(TuiApp *app);
void tui_app_open_chat(TuiApp *app, const char *jid);
void tui_app_apply_settings(TuiApp *app, const Settings *updated);
void tui_app_activate_message(TuiApp *app, int index);
/* Opens a message whatever else Enter would do with it: the reader for long text, the viewer or player for media. */
void tui_app_open_message(TuiApp *app, int index);
void tui_app_viewer_action(TuiApp *app, ImageViewerAction action);
void tui_app_send_composer(TuiApp *app);
void tui_app_toggle_recording(TuiApp *app);
void tui_app_open_file_picker(TuiApp *app, FilePickerPurpose purpose);
void tui_app_file_picked(TuiApp *app, const char *path);
void tui_app_attach(TuiApp *app, const char *path);
void tui_app_paste_image(TuiApp *app);
/* The + button: a menu to take a photo or choose a file. */
void tui_app_open_attach_menu(TuiApp *app);
void tui_app_apply_attach_choice(TuiApp *app);
/* The camera viewfinder, to take a photo for the open chat. */
void tui_app_open_camera(TuiApp *app);
/* Saves how the chat list groups are folded, when it changed. */
void tui_app_save_folding(TuiApp *app);
/* What a key in the camera view asked for: snap, use, retake or cancel. */
void tui_app_camera_action(TuiApp *app, CameraViewAction action);
void tui_app_toggle_soft_lock(TuiApp *app, const char *jid);
void tui_app_confirmed(TuiApp *app);
void tui_app_call_choice(TuiApp *app, IncomingCallChoice choice);
void tui_app_open_contact(TuiApp *app, const char *jid);
void tui_app_contact_action(TuiApp *app);
void tui_app_show_portrait(TuiApp *app, const char *jid);
const char *tui_app_member_name(void *app, const char *jid);
void tui_app_toggle_soft_lock_here(TuiApp *app);
void tui_app_start_reply(TuiApp *app, int index);
void tui_app_cancel_reply(TuiApp *app);
void tui_app_start_edit(TuiApp *app, int index);
void tui_app_cancel_edit(TuiApp *app);
void tui_app_open_message_menu(TuiApp *app, int index, int y, int x);
void tui_app_open_delete_menu(TuiApp *app, int index, int y, int x);
void tui_app_save_message(TuiApp *app, int index);
int  tui_app_show_message(TuiApp *app, const char *id);
/* Goes to the newest message of the open chat, loading it again if the view had moved far back. */
void tui_app_show_latest(TuiApp *app);
/* Loads an older page of the open chat, keeping the screen and the selection where they are. */
int  tui_app_load_older(TuiApp *app);
void tui_app_go_to_quote(TuiApp *app, int index);
void tui_app_apply_message_action(TuiApp *app);
void tui_app_open_reactions(TuiApp *app, int index);
/* Full emoji picker: insert at the caret, or react to message_id. */
void tui_app_open_emoji(TuiApp *app, EmojiPickerPurpose purpose, const char *message_id, const char *query);
void tui_app_emoji_chosen(TuiApp *app);
void tui_app_reaction_chosen(TuiApp *app);
void tui_app_open_chat_options(TuiApp *app, const char *jid);
void tui_app_apply_chat_option(TuiApp *app);
void tui_app_open_theme_picker(TuiApp *app, const char *jid);
void tui_app_finish_theme_picker(TuiApp *app, int keep);
void tui_app_preview_chat_theme(TuiApp *app);
void tui_app_open_search(TuiApp *app, const char *query);
void tui_app_run_search(TuiApp *app);
void tui_app_choose_search_result(TuiApp *app);
void tui_app_open_help(TuiApp *app);
void tui_app_start_screensaver(TuiApp *app);
void tui_app_toggle_dnd(TuiApp *app);
void tui_app_quit(TuiApp *app);
const NameResolver *tui_app_names(TuiApp *app);

/* tui_account.c: your profile and statuses */
void tui_app_open_profile(TuiApp *app);
void tui_app_profile_request(TuiApp *app, ProfileDialogsRequest request);
void tui_app_open_status(TuiApp *app);
void tui_app_status_request(TuiApp *app, StatusComposerRequest request);
/* A file or camera picture or video picked for your photo or a status. */
void tui_app_account_file(TuiApp *app, FilePickerPurpose purpose, const char *path);
void tui_app_status_paste_picture(TuiApp *app);
/* Ticks the account and status managers and shows how changes ended. */
void tui_app_account_tick(TuiApp *app);
/* Fills `model` for the profile view; `about` holds the text it points to. */
void tui_app_profile_model(TuiApp *app, ProfileViewModel *model, char *about, size_t about_size);
/* Your name for the header: as changed here, else as linked. */
const char *tui_app_user_name(TuiApp *app);
void tui_app_switch_to_whatsmeow(TuiApp *app);
void tui_app_remove_profile_photo(TuiApp *app);
/* The camera, for your photo or a status rather than the open chat. */
void tui_app_open_camera_for(TuiApp *app, CameraPurpose purpose);

/* tui_statuses.c: looking at statuses */
void tui_app_open_statuses(TuiApp *app);
void tui_app_statuses_request(TuiApp *app, StatusFeedRequest request);
/* Marks the status in view as seen and fetches its media; call once per loop. */
void tui_app_statuses_tick(TuiApp *app);
void tui_app_statuses_render(TuiApp *app, UiRect area);
/* People with statuses you have not seen, for the header. */
int  tui_app_statuses_unseen(TuiApp *app);

/* tui_mentions.c: mentioning people in groups */
void tui_app_refresh_mention(TuiApp *app);
int  tui_app_mention_key(TuiApp *app, int is_key, int ch);
void tui_app_pick_mention(TuiApp *app, int index);
void tui_app_forget_mentions(TuiApp *app);
/* tui_forward.c: sending a message on to other chats */
void tui_app_ask_clear_input(TuiApp *app);
void tui_app_open_forward(TuiApp *app, int index);
void tui_app_forward_request(TuiApp *app, PopupResult result);
/* The chats an admin agent may answer its own requests in: the dialog, and what it answered. */
void tui_app_open_self_chats(TuiApp *app);

/* tui_scheduling.c: due messages of one account, and what to say about them */
int  tui_scheduling_send_due(SchedulingManager *scheduling, MessagingManager *messaging, int *late);
void tui_app_scheduling_report(TuiApp *app, int sent, int late);

/* tui_accounts.c: the accounts that are running besides the one in view */
void tui_app_accounts_start(TuiApp *app);
void tui_app_accounts_tick(TuiApp *app);
void tui_app_accounts_set_active(TuiApp *app, int active);
/* The chat list: the account in view alone when it is the only one running,
 * else every account's chats together, or those of the one the filter names. */
const Chat *tui_app_chat_rows(TuiApp *app, int *count);
/* How many accounts are running. */
int  tui_app_account_count(TuiApp *app);
/* Brings another account into view: its conversation, profile, statuses and
 * dialogs replace those of the one that was. Returns 0 when it is there. */
int  tui_app_use_account(TuiApp *app, AccountId account);
/* Opens the chat of a row of the list, through the account the row acts through. */
void tui_app_open_row(TuiApp *app, const Chat *row);
/* The selected row's chat, with its account brought into view so that what
 * is done next acts on the right one. NULL when no chat is selected. */
const char *tui_app_take_selected(TuiApp *app);
/* Shows one account's chats, or all (ACCOUNT_ID_NONE), and steps to the next choice. */
void tui_app_set_account_filter(TuiApp *app, AccountId account);
void tui_app_cycle_account_filter(TuiApp *app);
/* The label for the header: "All", or the account the list shows. Empty with one account. */
void tui_app_account_chip(TuiApp *app, char *out, size_t size);
/* The accounts dialog: opening it, what it asks for, and drawing it. */
void tui_app_open_accounts(TuiApp *app);
void tui_app_accounts_request(TuiApp *app, AccountsDialogRequest request);
void tui_app_accounts_render(TuiApp *app, UiRect area);
/* After a yes to removing or logging out the account a confirmation named. */
void tui_app_remove_account(TuiApp *app, AccountId account);
void tui_app_logout_account(TuiApp *app, AccountId account);
/* The open conversation: the account in view's messages, or, for a contact
 * merged across accounts, everyone's in the order they happened. */
const Message *tui_app_messages(TuiApp *app, int *count);
/* The account message `index` of that conversation belongs to. */
AccountId tui_app_message_account(TuiApp *app, int index);
/* Opens the chat of `row` in the other accounts it is merged with, and closes it there again. */
void tui_app_open_peers(TuiApp *app, const Chat *row);
void tui_app_close_peers(TuiApp *app);
/* Makes the managers of `services` the ones in view, changing nothing else. */
void tui_app_take_services(TuiApp *app, const AccountServices *services);
/* Within a merged conversation, makes another of its accounts the one that
 * acts and sends, keeping the conversation as it is. Any other account is
 * brought into view the ordinary way. */
int  tui_app_turn_to(TuiApp *app, AccountId account);
/* Acts through the account message `index` belongs to, saying so when that changes who sends. */
void tui_app_follow_message(TuiApp *app, int index);
/* New notifications over every running account. */
const UnreadTally *tui_app_tally(TuiApp *app);
/* Alt+Shift+A: the account that sends now is the one this contact is always sent to from. */
void tui_app_keep_send_account(TuiApp *app);
/* Alt+A: the next account that has this chat sends. */
void tui_app_cycle_send_account(TuiApp *app);
/* "as <label>" for the input; empty with one account or no chat open. */
void tui_app_send_label(TuiApp *app, char *out, size_t size);
/* Loads older messages of the open chat in every account it is merged with; 1 when any had more. */
int  tui_app_peers_load_older(TuiApp *app);
/* The contact card's settings for one chat: what they show, and stepping each to its next choice. */
void tui_app_refresh_contact_prefs(TuiApp *app);
void tui_app_step_send_from(TuiApp *app, const char *jid);
void tui_app_step_merge(TuiApp *app, const char *jid);
void tui_app_toggle_agent_answers(TuiApp *app, const char *jid);
/* TL;DR (tui_summaries.c). */
void tui_app_init_summaries(TuiApp *app);
/* Where the conversation of `chat` gets summaries, or NULL when it is not in TL;DR mode. */
const SummarySource *tui_app_summaries_for(TuiApp *app, const Chat *chat);
/* The contact card's TL;DR row: what it shows, and switching it. */
void tui_app_refresh_summary_prefs(TuiApp *app);
void tui_app_toggle_tldr(TuiApp *app, const char *jid);
/* Unfolds a summarised message to its original, or folds it back; 0 when the message has no summary. */
int  tui_app_toggle_summary(TuiApp *app, int index);
/* Voice note transcripts (tui_transcripts.c). */
void tui_app_init_transcripts(TuiApp *app);
/* Where the conversation of `chat` gets transcripts, or NULL when they are not shown in it. */
const TranscriptSource *tui_app_transcripts_for(TuiApp *app, const Chat *chat);
/* The contact card's two rows about transcripts: what they show, and stepping each. */
void tui_app_refresh_transcript_prefs(TuiApp *app);
void tui_app_step_show_transcripts(TuiApp *app, const char *jid);
void tui_app_toggle_transcribing(TuiApp *app, const char *jid);
/* The languages a chat's voice notes are spoken in: a list with a switch for each. */
/* With no chat named it is the list for every chat that names none of its own (Settings, Chats). */
void tui_app_open_voice_languages(TuiApp *app, const char *jid);
void tui_app_voice_languages_summary(TuiApp *app, char *out, size_t size);
void tui_app_voice_languages_render(TuiApp *app, UiRect area);
void tui_app_voice_languages_request(TuiApp *app, PopupResult result);
/* Whether a voice note has a transcript, and reading every one of them in full. */
int  tui_app_has_transcript(TuiApp *app, const Message *message, AccountId owner);
void tui_app_show_transcript(TuiApp *app, const Message *message, AccountId owner);
/* The contacts with a sending account of their own. */
void tui_app_open_send_accounts(TuiApp *app);
void tui_app_send_accounts_request(TuiApp *app, SendAccountRequest request);
void tui_app_send_accounts_render(TuiApp *app, UiRect area);
void tui_app_self_chats_request(TuiApp *app, PopupResult result);
/* "no chat", "3 chats" or "every chat agents may use". */
void tui_app_self_chats_summary(TuiApp *app, char *out, size_t size);

/* tui_scheduling.c: messages to send later */
/* "/later <when> <text>" for the open chat. */
void tui_app_schedule_later(TuiApp *app, const char *args);
/* tui_agents.c */
void tui_app_open_agents(TuiApp *app);
void tui_app_agents_tick(TuiApp *app);
void tui_app_agents_key(TuiApp *app, int is_key, int ch);
void tui_app_agents_click(TuiApp *app, int y, int x);
void tui_app_agents_render(TuiApp *app, UiRect area);
/* Settings changed elsewhere (another client): follow the theme and the mouse. */
void tui_app_follow_settings(TuiApp *app);
/* Applies the open chat's own theme to the conversation (or the app's); `force` even when unchanged. */
void tui_app_sync_conversation_theme(TuiApp *app, int force);

void tui_app_open_scheduled(TuiApp *app);
void tui_app_scheduled_request(TuiApp *app, ScheduledListRequest request);
void tui_app_scheduled_render(TuiApp *app, UiRect area);
/* Sends what is due while connected; call once per loop. */
void tui_app_scheduling_tick(TuiApp *app);

/* tui_commands.c */
const SlashCommand *tui_commands_all(int *count);
/* Runs "/name args". Returns 1 when the line was a command. */
int  tui_commands_run(TuiApp *app, const char *line);

/* tui_input.c */
void tui_input_dispatch(TuiApp *app, int is_key_code, int ch);

/* tui_render.c */
void tui_render_frame(TuiApp *app, int64_t now_ms);

#endif
