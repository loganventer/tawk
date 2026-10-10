#include "core/settings_schema.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

#define B(cat, key, label, help, member) \
    { cat, key, label, help, SETTING_KIND_BOOL, offsetof(Settings, member), 0, 0, 1, 1, NULL, 0 }
#define I(cat, key, label, help, member, mn, mx, st, rs) \
    { cat, key, label, help, SETTING_KIND_INT, offsetof(Settings, member), 0, mn, mx, st, NULL, rs }
#define S(cat, key, label, help, member, rs) \
    { cat, key, label, help, SETTING_KIND_STRING, offsetof(Settings, member), sizeof(((Settings *)0)->member), 0, 0, 0, NULL, rs }

static const SettingField FIELDS[] = {
    { SETTING_CATEGORY_APPEARANCE, "theme", "Theme", "Colour theme; step through the list to preview, Enter to apply",
      SETTING_KIND_THEME, offsetof(Settings, theme), sizeof(((Settings *)0)->theme), 0, 0, 0, NULL, 0 },
    I(SETTING_CATEGORY_APPEARANCE, "sidebar_width", "Sidebar width", "Chat list width in columns", sidebar_width, 20, 80, 2, 0),
    { SETTING_CATEGORY_APPEARANCE, "chat_list_style", "Chat list", "detailed shows the last message under each chat; compact shows one line per chat",
      SETTING_KIND_CHOICE, offsetof(Settings, chat_list_style), sizeof(((Settings *)0)->chat_list_style), 0, 0, 0, "detailed|compact", 0 },
    I(SETTING_CATEGORY_APPEARANCE, "chat_spacing", "Space between chats", "Blank lines between chats in the list (0 to 2)", chat_spacing, 0, 2, 1, 0),
    B(SETTING_CATEGORY_APPEARANCE, "pinned_folded", "Fold pinned chats", "Kept up to date as you fold the Pinned group (Enter on its header)", pinned_folded),
    B(SETTING_CATEGORY_APPEARANCE, "chats_folded", "Fold other chats", "Kept up to date as you fold the Chats group", chats_folded),
    B(SETTING_CATEGORY_APPEARANCE, "sidebar_collapsed", "Start with sidebar collapsed", "Ctrl+B toggles it at any time", sidebar_collapsed),
    B(SETTING_CATEGORY_APPEARANCE, "use_24h_clock", "24-hour clock", "Show 14:05 instead of 2:05 PM", use_24h_clock),
    B(SETTING_CATEGORY_APPEARANCE, "inline_thumbnails", "Photo previews", "Show photos and video previews in the chat; click one to open it", inline_thumbnails),
    B(SETTING_CATEGORY_APPEARANCE, "portraits", "Profile pictures", "Show contacts' and groups' pictures in the chat list and the title bar", portraits),
    { SETTING_CATEGORY_APPEARANCE, "image_mode", "Photo quality", "sixel draws real pixels (Windows Terminal, WezTerm, foot); blocks works everywhere; auto picks",
      SETTING_KIND_CHOICE, offsetof(Settings, image_mode), sizeof(((Settings *)0)->image_mode), 0, 0, 0, "auto|sixel|blocks", 0 },
    B(SETTING_CATEGORY_APPEARANCE, "mouse", "Mouse support", "Click chats, scroll, and open media", mouse),
    B(SETTING_CATEGORY_APPEARANCE, "splash", "Startup splash", "Show the animated logo while tawk connects; any key skips it", splash),

    B(SETTING_CATEGORY_CHATS, "format_text", "Text formatting", "Show *bold*, _italic_, ~strikethrough~ and `code` as WhatsApp does; off shows the marks", format_text),
    B(SETTING_CATEGORY_CHATS, "link_previews", "Link previews for links you send", "Fetches the page to show its title and picture; the site sees your IP address. Previews others send always show", link_previews),
    B(SETTING_CATEGORY_CHATS, "convert_emoticons", "Emoticons to emoji", "Turn :) <3 :D and (pizza) into emoji as you type", convert_emoticons),
    B(SETTING_CATEGORY_CHATS, "enter_sends", "Enter is send", "When off, Enter adds a new line and Ctrl+S sends", enter_sends),
    I(SETTING_CATEGORY_CHATS, "message_margin", "Messages kept around the screen", "How many messages stay in memory before and after the ones on screen", message_margin, 20, 500, 10, 0),
    B(SETTING_CATEGORY_CHATS, "share_typing", "Share typing", "Show \"typing\u2026\" to the other person while you type", share_typing),
    B(SETTING_CATEGORY_CHATS, "appear_online", "Appear online", "Show as online while tawk is in use (needed to see others typing)", appear_online),
    B(SETTING_CATEGORY_CHATS, "show_online", "Show online status", "Show \"online\" or \"last seen\" under the name of the open chat, for people who share it with you (needs Appear online)", show_online),
    B(SETTING_CATEGORY_CHATS, "show_transcripts", "Voice note transcripts", "Show the words of a voice note under it when it has been transcribed; a chat can say otherwise on its contact card", show_transcripts),
    I(SETTING_CATEGORY_CHATS, "tldr_from_chars", "TL;DR from (characters)", "In a chat with TL;DR switched on (its contact card), a message at least this long is summarised; 0 summarises every message", tldr_min_chars, 0, 5000, 50, 0),
    I(SETTING_CATEGORY_CHATS, "awaiting_days", "Awaiting a reply after (days)", "A one-to-one chat whose last message is yours and this many days old shows under /filter awaiting", awaiting_days, 1, 60, 1, 0),
    I(SETTING_CATEGORY_CHATS, "tldr_back_days", "TL;DR back (days)", "How many days back a TL;DR chat's older long messages are summarised by themselves, newest first; 0 summarises only what you look at", tldr_back_days, 0, 365, 1, 0),
    B(SETTING_CATEGORY_CHATS, "reopen_last_chat", "Reopen last chat", "Open the chat you had open when tawk last quit", reopen_last_chat),
    B(SETTING_CATEGORY_CHATS, "merge_accounts", "Merge the same contact across my numbers", "Someone who writes to several of your accounts shows as one chat; a contact can be set apart on its contact card", merge_accounts),
    S(SETTING_CATEGORY_CHATS, "last_chat", "Last chat", "Kept up to date as you open chats", last_chat, 0),
    S(SETTING_CATEGORY_CHATS, "recent_emoji", "Recent emoji", "Kept up to date by the emoji picker", recent_emoji, 0),
    I(SETTING_CATEGORY_CHATS, "status_keep_days", "Keep statuses (days)", "WhatsApp shows a status for a day; tawk keeps it this long in the Status archive (1 keeps none)", status_keep_days, 1, 365, 1, 0),
    B(SETTING_CATEGORY_CHATS, "send_read_receipts", "Read receipts", "Tell senders when you have read their messages", send_read_receipts),

    B(SETTING_CATEGORY_NOTIFICATIONS, "enabled", "Notifications", "Master switch for all alerts", notifications),
    B(SETTING_CATEGORY_NOTIFICATIONS, "do_not_disturb", "Do not disturb", "Silence everything; Ctrl+D toggles it", do_not_disturb),
    B(SETTING_CATEGORY_NOTIFICATIONS, "mention_notifications", "Mentions always notify", "Being @mentioned notifies you even in a muted chat or with group notifications off (not during do not disturb)", mention_notifications),
    B(SETTING_CATEGORY_NOTIFICATIONS, "group_notifications", "Group notifications", "Alert for group messages", group_notifications),
    B(SETTING_CATEGORY_NOTIFICATIONS, "show_preview", "Show preview", "Include message text in the title bar", show_preview),
    { SETTING_CATEGORY_NOTIFICATIONS, "system_notifications", "System notifications", "A banner outside tawk for a new message, for when its tab is not in view: terminal asks the terminal itself (iTerm2, kitty, WezTerm, Ghostty, foot), desktop runs terminal-notifier, osascript or notify-send, both does both",
      SETTING_KIND_CHOICE, offsetof(Settings, system_notifications), sizeof(((Settings *)0)->system_notifications), 0, 0, 0, "off|terminal|desktop|both", 0 },
    S(SETTING_CATEGORY_NOTIFICATIONS, "quiet_hours", "Quiet hours", "A stretch of the day with no alerts, such as 22:00-07:00; a mention still gets through when mentions always notify. Empty: none", quiet_hours, 0),
    S(SETTING_CATEGORY_NOTIFICATIONS, "quiet_hours_weekend", "Quiet hours at weekends", "Other quiet hours for Saturday and Sunday, such as 23:00-09:00. Empty: the same as on weekdays", quiet_hours_weekend, 0),
    B(SETTING_CATEGORY_NOTIFICATIONS, "sound", "Notification sound", "Play a sound for new messages", sound),
    S(SETTING_CATEGORY_NOTIFICATIONS, "sound_file", "Sound file", "WAV/OGG file to play", sound_file, 0),
    B(SETTING_CATEGORY_NOTIFICATIONS, "blink", "Blink new chats", "Blink the chat in the list and the status bar", blink),
    I(SETTING_CATEGORY_NOTIFICATIONS, "blink_seconds", "Blink duration (s)", "How long the blink lasts", blink_seconds, 1, 60, 1, 0),
    B(SETTING_CATEGORY_NOTIFICATIONS, "title_flash", "Flash window title", "Show per-type unread counts in the terminal title and flash it", title_flash),
    B(SETTING_CATEGORY_NOTIFICATIONS, "terminal_bell", "Terminal bell", "Ring the bell; Windows Terminal flashes the taskbar", terminal_bell),
    B(SETTING_CATEGORY_NOTIFICATIONS, "screen_flash", "Screen flash", "Flash the whole screen once", screen_flash),

    B(SETTING_CATEGORY_MEDIA, "auto_download", "Auto-download media", "Fetch photos and videos when they arrive", auto_download_media),
    I(SETTING_CATEGORY_MEDIA, "auto_download_max_mb", "Auto-download limit (MB)", "Larger files download when clicked", auto_download_max_mb, 1, 512, 1, 0),
    S(SETTING_CATEGORY_MEDIA, "image_viewer", "Image viewer", "builtin shows photos inside tawk; system uses the default viewer; or a command such as eog", image_viewer, 0),
    S(SETTING_CATEGORY_MEDIA, "video_player", "Video player", "Empty tries mpv, vlc, celluloid, totem, ffplay, then the system default; or a command", video_player, 0),
    S(SETTING_CATEGORY_MEDIA, "download_dir", "Save folder", "Where Save to Downloads puts files; empty uses your Downloads folder", download_dir, 0),
    S(SETTING_CATEGORY_MEDIA, "attach_dir", "Attachment folder", "Where the file picker opens; kept up to date as you attach files", attach_dir, 0),
    I(SETTING_CATEGORY_MEDIA, "media_cache_mb", "Media cache limit (MB)", "Oldest downloads are removed above this size", media_cache_mb, 50, 100000, 50, 0),
    S(SETTING_CATEGORY_MEDIA, "media_dir", "Media folder", "Downloaded media (a cache: safe to delete)", media_dir, 1),

    { SETTING_CATEGORY_MEDIA, "audio_backend", "Audio system", "auto detects PulseAudio, PipeWire, ALSA, CoreAudio or DirectShow",
      SETTING_KIND_CHOICE, offsetof(Settings, audio_backend), sizeof(((Settings *)0)->audio_backend), 0, 0, 0, "auto|pulse|pipewire|alsa|coreaudio|dshow", 0 },
    S(SETTING_CATEGORY_MEDIA, "mic_device", "Microphone", "Capture device for the audio system; \"default\" uses the system default", mic_device, 0),
    I(SETTING_CATEGORY_MEDIA, "voice_max_seconds", "Max voice note length (s)", "Recording stops and sends at this length", voice_max_seconds, 10, 900, 10, 0),
    B(SETTING_CATEGORY_SCREENSAVER, "enabled", "Screensaver", "Run a command after a period of inactivity", screensaver),
    I(SETTING_CATEGORY_SCREENSAVER, "lock_minutes", "Lock after (minutes)", "With encrypted chats: tawk shows nothing until their passphrase is typed, after this many minutes without a key. 0 locks only when you press Ctrl+L or type /lock", lock_minutes, 0, 240, 1, 0),
    I(SETTING_CATEGORY_SCREENSAVER, "idle_minutes", "Idle minutes", "Inactivity before the screensaver starts", idle_minutes, 1, 240, 1, 0),
    S(SETTING_CATEGORY_SCREENSAVER, "command", "Command", "Shell command to run; any key stops it", screensaver_command, 0),
    B(SETTING_CATEGORY_SCREENSAVER, "wake_on_message", "Wake on message", "Stop the screensaver when a message arrives", wake_on_message),

    I(SETTING_CATEGORY_RESILIENCE, "backoff_initial_ms", "Initial retry delay (ms)", "First reconnect delay; doubles each attempt", backoff_initial_ms, 100, 60000, 100, 0),
    I(SETTING_CATEGORY_RESILIENCE, "backoff_max_ms", "Maximum retry delay (ms)", "Upper bound for the exponential backoff", backoff_max_ms, 1000, 600000, 1000, 0),
    I(SETTING_CATEGORY_RESILIENCE, "breaker_threshold", "Circuit breaker threshold", "Consecutive failures before retries pause", breaker_threshold, 1, 50, 1, 0),
    I(SETTING_CATEGORY_RESILIENCE, "breaker_cooldown_s", "Circuit breaker cooldown (s)", "Pause before a trial reconnect", breaker_cooldown_s, 5, 3600, 5, 0),

    B(SETTING_CATEGORY_AUTOMATION, "control_socket", "Agent access (MCP)", "Let tawk-mcp and the tawk send, tail and unread commands reach this tawk; off, nothing can connect", control_socket),
    { SETTING_CATEGORY_AUTOMATION, "access", "What they may do", "read lists and reads chats; send also sends, reacts, schedules and drafts; manage changes chats, statuses, your profile and settings. You allow each change. admin also lets a program holding the admin token answer its own sends in the chats you choose for that",
      SETTING_KIND_CHOICE, offsetof(Settings, automation_access), sizeof(((Settings *)0)->automation_access), 0, 0, 0, "read|send|manage|admin", 0 },
    S(SETTING_CATEGORY_AUTOMATION, "chats", "Chats they may use", "Names or numbers, separated by commas; empty allows every chat except locked ones", automation_chats, 0),
    B(SETTING_CATEGORY_AUTOMATION, "confirm_cli", "Ask for shell commands too", "tawk send asks first as well; programs acting for a model always ask", automation_confirm_cli),
    I(SETTING_CATEGORY_AUTOMATION, "writes_per_minute", "Writes per minute", "More than this are refused until a minute has passed", automation_rate, 1, 60, 1, 0),
    B(SETTING_CATEGORY_AUTOMATION, "ai_disclaimer", "Add AI disclaimer", "Messages an agent sends, schedules or replies to a status with get a line underneath saying an AI wrote them", automation_disclaimer),
    S(SETTING_CATEGORY_AUTOMATION, "ai_disclaimer_text", "Disclaimer text", "The line added under those messages", automation_disclaimer_text, 0),
    B(SETTING_CATEGORY_AUTOMATION, "push_received", "Push received messages", "Agents that listen hear about each message other people send, as it arrives; off, they see messages only when they read a chat", automation_push_received),
    B(SETTING_CATEGORY_AUTOMATION, "push_sent", "Push messages you send", "Agents that listen hear about each message you send too; off, they see yours only when they read a chat", automation_push_sent),
    S(SETTING_CATEGORY_AUTOMATION, "self_approval_chats", "Chats answered by an agent itself", "With access admin: the chats an agent holding the admin token may answer its own sends in; empty allows none. Chosen under Settings, Automation", automation_self_chats, 0),
    B(SETTING_CATEGORY_AUTOMATION, "push_read", "Push read receipts", "Agents that listen hear when someone reads a message you sent; off, they are not told", automation_push_read),
    B(SETTING_CATEGORY_AUTOMATION, "push_reactions", "Push reactions", "Agents that listen hear when someone reacts to a message you sent, or takes a reaction back", automation_push_reactions),
    B(SETTING_CATEGORY_AUTOMATION, "push_edits", "Push edits and deletes", "Agents that listen hear when someone changes or deletes a message they sent", automation_push_edits),
    B(SETTING_CATEGORY_AUTOMATION, "push_scheduled", "Push scheduled sends", "Agents that listen hear when a message you scheduled goes out", automation_push_scheduled),
    B(SETTING_CATEGORY_AUTOMATION, "push_presence", "Push online status", "Agents that listen hear when the person in the chat you have open comes online or leaves; off, they are not told", automation_push_presence),
    B(SETTING_CATEGORY_AUTOMATION, "presence_lookup", "Look up online status", "An agent may ask whether the person in a chat is online or when they were last seen, one person at a time, for the chats it may use; off, it cannot ask", automation_presence_lookup),
    I(SETTING_CATEGORY_AUTOMATION, "self_approvals_per_hour", "Self-approvals per hour", "With access admin: how many of its own requests a program may answer in an hour; past this they wait for you", automation_self_per_hour, 1, 240, 1, 0),
    { SETTING_CATEGORY_AUTOMATION, "transcribe_model", "Transcription model", "The Whisper model an agent's transcriber uses for voice notes: large-v3-turbo suits most languages, tiny is the quickest and lightest, larger ones are more accurate and slower",
      SETTING_KIND_CHOICE, offsetof(Settings, transcribe_model), sizeof(((Settings *)0)->transcribe_model), 0, 0, 0, "tiny|base|small|medium|large-v3-turbo|large-v3", 0 },
    S(SETTING_CATEGORY_AUTOMATION, "transcribe_languages", "Transcription languages", "Language codes separated by commas, such as af,en, or auto; each one gets its own transcription of a voice note", transcribe_languages, 0),
    B(SETTING_CATEGORY_AUTOMATION, "transcribe_auto", "Transcribe voice notes as they arrive", "An agent's transcriber writes out every voice note other people send, without being asked; off, only the ones an agent asks for", transcribe_auto),

    B(SETTING_CATEGORY_AUTOMATION, "mask_codes", "Hide codes and card numbers from agents", "One-time codes and card numbers in messages reach an agent acting for a model as [code] and [card number]; your own shell commands see the text as it is", automation_mask_codes),
    S(SETTING_CATEGORY_AUTOMATION, "owner_chat", "Owner's chat", "The account whose \"message yourself\" chat is your chat with the agent: what you type there on your phone reaches your agent as your words, and it answers you there by itself. Set on that chat's contact card; empty is off", owner_chat, 0),
    I(SETTING_CATEGORY_AUTOMATION, "owner_replies_per_hour", "Owner's chat: answers per hour", "How many answers an agent may send you in the owner's chat by itself in an hour; past this they wait for you like any send", owner_replies_per_hour, 1, 600, 5, 0),
    B(SETTING_CATEGORY_AUTOMATION, "owner_approvals", "Owner's chat: approve from WhatsApp", "A send that waits for your answer in tawk is also put to you in the owner's chat, to allow or decline from your phone; deletes, blocks, settings and first messages to someone new never are", owner_approvals),
    I(SETTING_CATEGORY_AUTOMATION, "owner_card_wait", "Owner's chat: wait before asking (seconds)", "How long a request waits in tawk before it is put to you on WhatsApp; 0 asks there at once", owner_card_wait, 0, 600, 5, 0),
    S(SETTING_CATEGORY_AUTOMATION, "default_agent", "Default agent", "The agent tawk turns to by itself, which writes TL;DR summaries: chosen in the Agents list (d) or by answering tawk's question on WhatsApp; empty uses the only one connected, or asks", default_agent, 0),

    { SETTING_CATEGORY_ADVANCED, "backend", "WhatsApp backend", "whatsmeow runs in-process; baileys runs a Node.js sidecar",
      SETTING_KIND_CHOICE, offsetof(Settings, backend), sizeof(((Settings *)0)->backend), 0, 0, 0, "whatsmeow|baileys", 1 },
    S(SETTING_CATEGORY_ADVANCED, "data_dir", "Data folder", "Chat database and WhatsApp login", data_dir, 1),
    S(SETTING_CATEGORY_ADVANCED, "sidecar_dir", "Sidecar folder", "Location of the WhatsApp bridge", sidecar_dir, 1),
    S(SETTING_CATEGORY_ADVANCED, "node_binary", "Node.js binary", "Runtime used for the bridge", node_binary, 1),
    { SETTING_CATEGORY_ADVANCED, "log_level", "Log level", "debug, info, warn or error",
      SETTING_KIND_CHOICE, offsetof(Settings, log_level), sizeof(((Settings *)0)->log_level), 0, 0, 0, "debug|info|warn|error", 1 },
};

#define FIELD_COUNT ((int)(sizeof(FIELDS) / sizeof(FIELDS[0])))

int settings_schema_count(void) { return FIELD_COUNT; }

const SettingField *settings_schema_at(int index) {
    return (index >= 0 && index < FIELD_COUNT) ? &FIELDS[index] : NULL;
}

const SettingField *settings_schema_find(SettingCategory category, const char *key) {
    for (int i = 0; i < FIELD_COUNT; i++) {
        if (FIELDS[i].category == category && strcmp(FIELDS[i].key, key) == 0) return &FIELDS[i];
    }
    return NULL;
}

int setting_get_int(const Settings *s, const SettingField *f) {
    return *(const int *)((const char *)s + f->offset);
}

void setting_set_int(Settings *s, const SettingField *f, int value) {
    if (f->kind == SETTING_KIND_BOOL) value = value ? 1 : 0;
    else if (f->kind == SETTING_KIND_INT) {
        if (value < f->min) value = f->min;
        if (value > f->max) value = f->max;
    }
    *(int *)((char *)s + f->offset) = value;
}

const char *setting_get_string(const Settings *s, const SettingField *f) {
    return (const char *)s + f->offset;
}

static int choice_allowed(const char *choices, const char *value) {
    char copy[128];
    str_copy(copy, sizeof(copy), choices);
    char *save = NULL;
    for (char *tok = strtok_r(copy, "|", &save); tok; tok = strtok_r(NULL, "|", &save)) {
        if (strcmp(tok, value) == 0) return 1;
    }
    return 0;
}

void setting_set_string(Settings *s, const SettingField *f, const char *value) {
    if (f->kind == SETTING_KIND_CHOICE && !choice_allowed(f->choices, value)) return;
    char *dst = (char *)s + f->offset;
    str_copy(dst, f->size, value);
    str_strip_controls(dst);
}

void setting_set_from_text(Settings *s, const SettingField *f, const char *text) {
    switch (f->kind) {
        case SETTING_KIND_BOOL:
            setting_set_int(s, f, str_parse_bool(text, setting_get_int(s, f)));
            break;
        case SETTING_KIND_INT:
            setting_set_int(s, f, str_parse_int(text, f->min, f->max, setting_get_int(s, f)));
            break;
        default:
            setting_set_string(s, f, text);
            break;
    }
}

void setting_to_text(const Settings *s, const SettingField *f, char *out, size_t size) {
    switch (f->kind) {
        case SETTING_KIND_BOOL: str_copy(out, size, setting_get_int(s, f) ? "true" : "false"); break;
        case SETTING_KIND_INT:  snprintf(out, size, "%d", setting_get_int(s, f)); break;
        default:                str_copy(out, size, setting_get_string(s, f)); break;
    }
}
