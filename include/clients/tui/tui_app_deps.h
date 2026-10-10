#ifndef APP_CLIENTS_TUI_TUI_APP_DEPS_H
#define APP_CLIENTS_TUI_TUI_APP_DEPS_H

#include <signal.h>

#include "clients/i_account_directory.h"
#include "clients/tui/approval_queue.h"
#include "managers/account_roster_manager.h"
#include "clients/tui/blink_state.h"
#include "clients/tui/title_flasher.h"
#include "contracts/i_audio_player.h"
#include "contracts/i_clipboard.h"
#include "contracts/i_clipboard_image.h"
#include "contracts/i_document_pages.h"
#include "contracts/i_emoji_catalog.h"
#include "contracts/i_frame_hook.h"
#include "contracts/i_idle_action.h"
#include "contracts/i_notifier.h"
#include "contracts/i_video_poster.h"
#include "managers/account_manager.h"
#include "managers/automation_manager.h"
#include "managers/database_crypt_manager.h"
#include "managers/media_manager.h"
#include "managers/messaging_manager.h"
#include "managers/call_manager.h"
#include "managers/profile_manager.h"
#include "managers/settings_manager.h"
#include "managers/scheduling_manager.h"
#include "managers/status_feed_manager.h"
#include "managers/status_manager.h"
#include "managers/summary_manager.h"
#include "managers/transcript_manager.h"

/* Everything the terminal client uses, injected by the composition root. */
typedef struct TuiAppDeps {
    MessagingManager      *messaging;
    ProfileManager        *profiles;
    CallManager           *calls;
    MediaManager          *media;
    SettingsManager       *settings;
    IIdleAction           *screensaver;
    DatabaseCryptManager  *crypt;                 /* whether your chats are encrypted, and what opens them; may be NULL */
    IAudioPlayer          *sound_player;
    IEmojiCatalog         *emoji;
    IClipboard            *clipboard;
    IVideoPoster          *video_posters;
    IDocumentPages        *document_pages;
    IClipboardImage       *clipboard_image;
    INotifier             *notifier;
    BlinkState            *blink;
    TitleFlasher          *title;
    const char            *backend_name;
    const char            *audio_backend_name;
    const char            *user_theme_dir;
    int                    sixel_supported;   /* the terminal draws Sixel images */
    int                    cell_width_px;     /* pixel size of a character cell, for Sixel */
    int                    cell_height_px;
    volatile sig_atomic_t *quit_requested;
    AccountManager        *accounts;
    StatusManager         *statuses;
    int                    whatsmeow_available;   /* this build can switch to the whatsmeow backend */
    /* Set by the client to have main restart tawk after a clean shutdown
     * (to switch backends); main clears any --backend override first. */
    volatile sig_atomic_t *restart_requested;
    StatusFeedManager     *feed;                  /* statuses to view, yours and others' */
    SchedulingManager     *scheduling;            /* messages to send later */
    AutomationManager     *automation;            /* what agents may do, and what they did */
    ApprovalQueue         *approvals;             /* their requests waiting for you (the Agents tab) */
    IFrameHook            *frame_hook;            /* another client's work each frame (the control socket); may be NULL */
    /* The managers above are those of the account in view. The directory holds
     * every running account's, and the roster names and orders them. */
    IAccountDirectory     *directory;
    AccountRosterManager  *roster;
    AccountId              active_account;
    TranscriptManager     *transcripts;           /* voice note transcripts and your choices about them; may be NULL */
    SummaryManager        *summaries;             /* TL;DR summaries and which chats are in that mode; may be NULL */
} TuiAppDeps;

#endif
