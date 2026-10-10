#include "clients/tui/settings_menu.h"

#include <stddef.h>

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define FIELD(cat, k) { MENU_NODE_FIELD, "", NULL, NULL, cat, k, MENU_ACTION_NONE, MENU_INFO_STATIC, NULL, 0 }
#define ACTION(icon, title, act) { MENU_NODE_ACTION, icon, title, NULL, 0, NULL, act, MENU_INFO_STATIC, NULL, 0 }
#define INFO(title, inf) { MENU_NODE_INFO, "", title, NULL, 0, NULL, MENU_ACTION_NONE, inf, NULL, 0 }
#define TEXT(title) INFO(title, MENU_INFO_STATIC)
#define SUB(icon, title, subtitle, kids) { MENU_NODE_SUBMENU, icon, title, subtitle, 0, NULL, MENU_ACTION_NONE, MENU_INFO_STATIC, kids, COUNT(kids) }

static const MenuNode ACCOUNT[] = {
    ACTION("\xF0\x9F\x91\xA5", "Accounts\xE2\x80\xA6", MENU_ACTION_ACCOUNTS),
    INFO("Name", MENU_INFO_NAME),
    INFO("Number", MENU_INFO_NUMBER),
    INFO("Connection", MENU_INFO_CONNECTION),
    ACTION("\xE2\x86\xBB", "Reconnect now", MENU_ACTION_RETRY_CONNECTION),
    ACTION("\xE2\x8E\x8B", "Log out (unlink this device)", MENU_ACTION_LOGOUT),
};

static const MenuNode CHATS[] = {
    FIELD(SETTING_CATEGORY_CHATS, "enter_sends"),
    FIELD(SETTING_CATEGORY_CHATS, "convert_emoticons"),
    FIELD(SETTING_CATEGORY_CHATS, "reopen_last_chat"),
    FIELD(SETTING_CATEGORY_CHATS, "merge_accounts"),
    FIELD(SETTING_CATEGORY_CHATS, "send_read_receipts"),
    FIELD(SETTING_CATEGORY_CHATS, "format_text"),
    FIELD(SETTING_CATEGORY_CHATS, "link_previews"),
    FIELD(SETTING_CATEGORY_CHATS, "share_typing"),
    FIELD(SETTING_CATEGORY_CHATS, "appear_online"),
    FIELD(SETTING_CATEGORY_CHATS, "show_online"),
    FIELD(SETTING_CATEGORY_CHATS, "show_transcripts"),
    FIELD(SETTING_CATEGORY_CHATS, "transcript_lines"),
    FIELD(SETTING_CATEGORY_CHATS, "tldr_min_chars"),
    FIELD(SETTING_CATEGORY_CHATS, "tldr_back_days"),
    FIELD(SETTING_CATEGORY_CHATS, "message_margin"),
    FIELD(SETTING_CATEGORY_CHATS, "status_keep_days"),
};

static const MenuNode ALERTS[] = {
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "enabled"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "do_not_disturb"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "group_notifications"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "mention_notifications"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "show_preview"),
    ACTION("\xF0\x9F\x94\x94", "Send a test notification", MENU_ACTION_TEST_NOTIFICATION),
};

static const MenuNode SOUND[] = {
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "sound"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "sound_file"),
    INFO("Audio system", MENU_INFO_AUDIO),
    ACTION("\xE2\x96\xB6", "Play test sound", MENU_ACTION_TEST_SOUND),
};

static const MenuNode VISUAL[] = {
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "blink"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "blink_seconds"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "title_flash"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "terminal_bell"),
    FIELD(SETTING_CATEGORY_NOTIFICATIONS, "screen_flash"),
};

static const MenuNode NOTIFICATIONS[] = {
    SUB("\xF0\x9F\x94\x94", "Alerts", "On/off, do not disturb, groups", ALERTS),
    SUB("\xF0\x9F\x94\x8A", "Sound", "Tone and audio system", SOUND),
    SUB("\xE2\x9C\xA8", "Visual", "Blink, window title, bell", VISUAL),
};

static const MenuNode THEME_PICKER[] = {
    { MENU_NODE_THEMES, "\xF0\x9F\x8E\xA8", "Choose theme", "Preview while you browse, Enter to apply",
      SETTING_CATEGORY_APPEARANCE, "theme", MENU_ACTION_NONE, MENU_INFO_STATIC, NULL, 0 },
    ACTION("\xE2\x86\xBB", "Reload theme files", MENU_ACTION_RELOAD_THEMES),
    INFO("Your themes folder", MENU_INFO_USER_THEMES),
};

static const MenuNode LAYOUT[] = {
    FIELD(SETTING_CATEGORY_APPEARANCE, "chat_list_style"),
    FIELD(SETTING_CATEGORY_APPEARANCE, "chat_spacing"),
    FIELD(SETTING_CATEGORY_APPEARANCE, "sidebar_width"),
    FIELD(SETTING_CATEGORY_APPEARANCE, "sidebar_collapsed"),
    FIELD(SETTING_CATEGORY_APPEARANCE, "use_24h_clock"),
    FIELD(SETTING_CATEGORY_APPEARANCE, "inline_thumbnails"),
    FIELD(SETTING_CATEGORY_APPEARANCE, "portraits"),
    FIELD(SETTING_CATEGORY_APPEARANCE, "mouse"),
    FIELD(SETTING_CATEGORY_APPEARANCE, "splash"),
};

static const MenuNode APPEARANCE[] = {
    SUB("\xF0\x9F\x8E\xA8", "Theme", "Colour schemes from JSON files", THEME_PICKER),
    SUB("\xE2\x96\xA6", "Layout", "Sidebar, clock, mouse", LAYOUT),
};

static const MenuNode PHOTOS_VIDEOS[] = {
    FIELD(SETTING_CATEGORY_MEDIA, "auto_download"),
    FIELD(SETTING_CATEGORY_MEDIA, "auto_download_max_mb"),
    FIELD(SETTING_CATEGORY_MEDIA, "image_viewer"),
    FIELD(SETTING_CATEGORY_MEDIA, "video_player"),
    FIELD(SETTING_CATEGORY_MEDIA, "download_dir"),
    FIELD(SETTING_CATEGORY_MEDIA, "attach_dir"),
    FIELD(SETTING_CATEGORY_MEDIA, "media_dir"),
    FIELD(SETTING_CATEGORY_MEDIA, "media_cache_mb"),
};

static const MenuNode VOICE[] = {
    FIELD(SETTING_CATEGORY_MEDIA, "audio_backend"),
    INFO("Detected audio system", MENU_INFO_AUDIO),
    FIELD(SETTING_CATEGORY_MEDIA, "mic_device"),
    FIELD(SETTING_CATEGORY_MEDIA, "voice_max_seconds"),
};

static const MenuNode MEDIA[] = {
    SUB("\xF0\x9F\x96\xBC", "Photos and videos", "Downloads, viewers, cache", PHOTOS_VIDEOS),
    SUB("\xF0\x9F\x94\x8A", "Voice notes", "Audio system and microphone", VOICE),
};

static const MenuNode SCREENSAVER[] = {
    FIELD(SETTING_CATEGORY_SCREENSAVER, "enabled"),
    FIELD(SETTING_CATEGORY_SCREENSAVER, "idle_minutes"),
    FIELD(SETTING_CATEGORY_SCREENSAVER, "command"),
    FIELD(SETTING_CATEGORY_SCREENSAVER, "wake_on_message"),
    ACTION("\xE2\x96\xB6", "Start screensaver now", MENU_ACTION_RUN_SCREENSAVER),
};

static const MenuNode BACKEND[] = {
    FIELD(SETTING_CATEGORY_ADVANCED, "backend"),
    INFO("Running backend", MENU_INFO_BACKEND),
    FIELD(SETTING_CATEGORY_ADVANCED, "node_binary"),
    FIELD(SETTING_CATEGORY_ADVANCED, "sidecar_dir"),
};

static const MenuNode RESILIENCE[] = {
    FIELD(SETTING_CATEGORY_RESILIENCE, "backoff_initial_ms"),
    FIELD(SETTING_CATEGORY_RESILIENCE, "backoff_max_ms"),
    FIELD(SETTING_CATEGORY_RESILIENCE, "breaker_threshold"),
    FIELD(SETTING_CATEGORY_RESILIENCE, "breaker_cooldown_s"),
};

static const MenuNode CONNECTION[] = {
    INFO("Status", MENU_INFO_CONNECTION),
    ACTION("\xE2\x86\xBB", "Reconnect now", MENU_ACTION_RETRY_CONNECTION),
    SUB("\xF0\x9F\x94\x8C", "Backend", "whatsmeow (native) or Baileys", BACKEND),
    SUB("\xF0\x9F\x9B\xA1", "Resilience", "Retry backoff and circuit breaker", RESILIENCE),
};

/* What agents that listen are told as it happens, one switch per kind of event. */
static const MenuNode AGENT_EVENTS[] = {
    FIELD(SETTING_CATEGORY_AUTOMATION, "push_received"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "push_sent"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "push_read"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "push_reactions"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "push_edits"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "push_scheduled"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "push_presence"),
    TEXT("An agent that listens (tawk-mcp's channel) hears each of these as it happens"),
    TEXT("Off, it still sees messages when it reads a chat"),
    TEXT("Your own tawk tail always shows messages, and none of the others"),
};

/* Access admin: where an agent holding the admin token may answer its own sends, and how often. */
static const MenuNode SELF_APPROVAL[] = {
    INFO("Answered by an agent itself in", MENU_INFO_SELF_CHATS),
    ACTION("\xE2\x9C\x93", "Choose the chats\xE2\x80\xA6", MENU_ACTION_SELF_APPROVAL_CHATS),
    FIELD(SETTING_CATEGORY_AUTOMATION, "self_approvals_per_hour"),
    TEXT("Only with What they may do set to admin, and only for an agent you gave the admin token"),
    TEXT("Sends, replies, scheduled messages, reactions, read marks and likes; never deletes or settings"),
    TEXT("Each one is logged as approved by the agent and shown on screen"),
};

/* How an agent's transcriber (tawk-mcp) writes out voice notes. tawk only holds the choices. */
static const MenuNode TRANSCRIPTION[] = {
    FIELD(SETTING_CATEGORY_AUTOMATION, "transcribe_model"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "transcribe_languages"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "transcribe_auto"),
    TEXT("tawk-mcp does the transcribing, on this computer, when it is started with --transcribe"),
    TEXT("Several languages give one transcription each, for voice notes that mix them"),
    TEXT("An agent can read these choices and cannot change them"),
};

/* The same submenu while no agent is connected: there is nobody to use those choices. */
static const MenuNode TRANSCRIPTION_IDLE_LINES[] = {
    TEXT("No agent connected"),
};
static const MenuNode TRANSCRIPTION_IDLE =
    SUB("\xF0\x9F\x8E\x99", "Voice note transcription", NULL, TRANSCRIPTION_IDLE_LINES);

static const MenuNode AUTOMATION_ABOUT[] = {
    TEXT("The Agentic tab (F3): requests to answer, who is connected, the log"),
    TEXT("Needs: tawk running with this on, and tawk-mcp in your MCP client"),
    TEXT("Risk: chat text an agent reads goes to its model's provider"),
    TEXT("Risk: messages others send you can try to steer an agent"),
    TEXT("Guard: sends and changes wait for you; deletes need two yeses"),
    TEXT("Guard: locked chats are never shown, and everything is logged"),
};

static const MenuNode AUTOMATION[] = {
    FIELD(SETTING_CATEGORY_AUTOMATION, "control_socket"),
    INFO("Now", MENU_INFO_AGENTS),
    FIELD(SETTING_CATEGORY_AUTOMATION, "access"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "chats"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "confirm_cli"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "writes_per_minute"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "presence_lookup"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "ai_disclaimer"),
    FIELD(SETTING_CATEGORY_AUTOMATION, "ai_disclaimer_text"),
    SUB("\xF0\x9F\x93\xA1", "Agent events", "What agents hear as it happens", AGENT_EVENTS),
    SUB("\xE2\x9C\x93", "Answering for itself", "With access admin: which chats, and how many an hour", SELF_APPROVAL),
    SUB("\xF0\x9F\x8E\x99", "Voice note transcription", "Model, languages, and whether every voice note is written out", TRANSCRIPTION),
    SUB("\xE2\x84\xB9", "Needs, risks and guards", "What agent access means", AUTOMATION_ABOUT),
};

static const MenuNode ADVANCED[] = {
    INFO("Config file", MENU_INFO_CONFIG_PATH),
    FIELD(SETTING_CATEGORY_ADVANCED, "data_dir"),
    FIELD(SETTING_CATEGORY_ADVANCED, "log_level"),
    ACTION("\xF0\x9F\xA7\xB9", "Clear logs", MENU_ACTION_CLEAR_LOGS),
};

static const MenuNode KEYS[] = {
    TEXT("Tab             cycle focus: chats, messages, input"),
    TEXT("Enter           open chat, send, open media"),
    TEXT("Ctrl+B          collapse or expand the chat list"),
    TEXT("Ctrl+R          record a voice note (Enter sends, Esc cancels)"),
    TEXT("Ctrl+K          search all messages"),
    TEXT("Ctrl+F          filter the chat list by name"),
    TEXT("Ctrl+E          emoji picker"),
    TEXT("Ctrl+O          attach a file"),
    TEXT("Alt+V           attach the picture on the clipboard"),
    TEXT("Ctrl+L          start the screensaver"),
    TEXT("Ctrl+N          next unread chat"),
    TEXT("Ctrl+D          do not disturb"),
    TEXT("/               slash commands in the input (/help lists them)"),
    TEXT("type            search the chat list by name"),
    TEXT("Alt+O/M/P/A     chat options, mute, pin, archive (chat list)"),
    TEXT("Alt+Q/E/M       reply, react, menu (selected message)"),
    TEXT("Alt+Shift+E     edit your message"),
    TEXT("Up (empty input) edit your last message, when it is the newest"),
    TEXT("Alt+R           retry a failed message"),
    TEXT("Right-click     message or chat menu (Ctrl+click on a Mac)"),
    TEXT("PgUp / PgDn     scroll the conversation"),
    TEXT("F2              settings"),
    TEXT("Ctrl+Q          quit"),
};

static const MenuNode ABOUT[] = {
    INFO("Version", MENU_INFO_VERSION),
    INFO("Made by", MENU_INFO_AUTHOR),
    SUB("\xE2\x8C\xA8", "Keyboard shortcuts", "Everything you can press", KEYS),
    TEXT("Open source, MIT licence"),
};

static const MenuNode ROOT_CHILDREN[] = {
    SUB("\xF0\x9F\x91\xA4", "Account", "Profile, linked device, log out", ACCOUNT),
    SUB("\xF0\x9F\x92\xAC", "Chats", "Enter key, read receipts, history", CHATS),
    SUB("\xF0\x9F\x94\x94", "Notifications", "Alerts, sound, blink, title bar", NOTIFICATIONS),
    SUB("\xF0\x9F\x8E\xA8", "Appearance", "Themes and layout", APPEARANCE),
    SUB("\xF0\x9F\x96\xBC", "Media", "Photos, videos, voice notes", MEDIA),
    SUB("\xF0\x9F\x8C\x99", "Screensaver", "Idle command such as matrix-clock", SCREENSAVER),
    SUB("\xF0\x9F\x94\x8C", "Connection", "Backend, reconnects, circuit breaker", CONNECTION),
    SUB("\xF0\x9F\xA4\x96", "Automation", "Agents: access, events, answering for itself", AUTOMATION),
    SUB("\xE2\x9A\x99", "Advanced", "Paths and logging", ADVANCED),
    SUB("\xE2\x84\xB9", "About", "Version and keyboard shortcuts", ABOUT),
};

static const MenuNode ROOT = SUB("\xE2\x9A\x99", "Settings", NULL, ROOT_CHILDREN);

const MenuNode *settings_menu_root(void) { return &ROOT; }

const MenuNode *settings_menu_shown(const MenuNode *menu, int agent_connected) {
    if (menu && menu->children == TRANSCRIPTION && !agent_connected) return &TRANSCRIPTION_IDLE;
    return menu;
}
