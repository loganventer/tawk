#ifndef APP_CORE_SETTINGS_H
#define APP_CORE_SETTINGS_H

/* Every user-tunable value. Persisted to ~/.config/APP_NAME/config.ini by
 * the settings store; edited live from the settings panel. */
#define SETTINGS_DEFAULT_THEME "whatsapp-dark"

typedef struct Settings {
    /* Appearance */
    char theme[32];
    int  sidebar_width;
    int  sidebar_collapsed;
    int  convert_emoticons;     /* ":)" becomes 🙂 as you type */
    int  reopen_last_chat;      /* open last_chat again when tawk starts */
    int  merge_accounts;        /* one contact on several of your accounts shows as one chat */
    char last_chat[128];        /* the chat open when tawk last ran (kept up to date) */
    int  chat_spacing;          /* blank lines between chats in the list, 0 to 2 */
    int  pinned_folded;         /* the Pinned group of the chat list is folded away */
    int  chats_folded;          /* the other chats are folded away */
    char chat_list_style[16];   /* detailed (two lines per chat) or compact (one line) */
    int  use_24h_clock;
    int  splash;                /* the animated logo while tawk starts */
    int  format_text;           /* show *bold*, _italic_ and the rest as WhatsApp does */
    int  mention_notifications; /* being @mentioned notifies even in muted chats */
    int  link_previews;         /* fetch a preview card for links you send (off: nothing is fetched) */
    int  mouse;
    int  inline_thumbnails;
    int  portraits;             /* profile pictures in the chat list and title bar */
    char image_mode[16];        /* auto, sixel (real pixels) or blocks (coloured half blocks) */

    /* Chats */
    int  enter_sends;
    int  message_margin;      /* messages kept in memory either side of the ones on screen */
    int  send_read_receipts;
    int  share_typing;          /* tell others when you are typing */
    int  appear_online;         /* show as online while tawk is in use */
    int  show_online;           /* show "online" or "last seen" under the name of the open chat */
    int  show_transcripts;      /* show a voice note's transcript under it, unless the chat says otherwise */
    int  tldr_min_chars;        /* a message at least this long is summarised in a chat in TL;DR mode; 0: every message */
    int  tldr_back_days;        /* how far back a TL;DR chat's older messages are summarised without being looked at; 0: not at all */
    char recent_emoji[256];
    int  status_keep_days;      /* how long statuses stay viewable here (WhatsApp shows them for one) */     /* space-separated, most recent first (kept by the picker) */

    /* Notifications */
    int  notifications;
    int  do_not_disturb;
    int  group_notifications;
    int  show_preview;
    int  sound;
    char sound_file[512];
    int  blink;
    int  blink_seconds;
    int  title_flash;
    int  terminal_bell;
    int  screen_flash;

    /* Media */
    int  auto_download_media;
    int  auto_download_max_mb;
    char image_viewer[256];     /* builtin (inside tawk, also when empty), system, or a command */
    char video_player[256];     /* empty = system default opener */
    char attach_dir[512];       /* where the file picker starts; the last folder used */
    char download_dir[512];     /* where "Save to Downloads" puts files; empty = the system downloads folder */
    char media_dir[512];
    int  media_cache_mb;        /* oldest files are pruned above this size */

    /* Voice notes */
    char audio_backend[16];     /* auto, pulse, pipewire, alsa, coreaudio, dshow */
    char mic_device[128];       /* ffmpeg input device, "default" */
    int  voice_max_seconds;

    /* Screensaver */
    int  screensaver;
    int  idle_minutes;
    char screensaver_command[512];
    int  wake_on_message;

    /* Resilience */
    int  backoff_initial_ms;
    int  backoff_max_ms;
    int  breaker_threshold;     /* consecutive failures before the circuit opens */
    int  breaker_cooldown_s;    /* open duration before a half-open trial */

    /* Automation: the control socket for tawk-mcp and shell commands */
    int  control_socket;        /* listen for other programs of yours (off: nothing can connect) */
    char automation_access[8];  /* read, send (messages you confirm), manage (everything else too) or admin (may answer its own sends) */
    char automation_chats[512]; /* comma-separated chats they may use; empty: all but locked ones */
    int  automation_confirm_cli;/* your own shell commands ask before sending too */
    int  automation_rate;       /* writes allowed per minute */
    int  automation_disclaimer; /* add a line under messages a program acting for a model sends, saying an AI wrote them */
    char automation_disclaimer_text[160]; /* that line */
    int  automation_push_received; /* agents that subscribe hear about messages other people send */
    int  automation_push_sent;  /* and about the ones you send */
    int  automation_push_read;  /* and when someone reads one you sent */
    int  automation_push_reactions; /* and when someone reacts to one you sent */
    int  automation_push_edits; /* and when someone edits or deletes one they sent */
    int  automation_push_scheduled; /* and when one you scheduled goes out */
    int  automation_push_presence; /* and when the person in the open chat comes online or leaves */
    int  automation_presence_lookup; /* agents may ask whether someone is online, one person at a time */
    char automation_self_chats[1024]; /* access admin: the chats (JIDs, comma-separated) a client may answer its own requests in; empty: none */
    int  automation_self_per_hour; /* access admin: requests a client may answer itself in an hour */
    char transcribe_model[24];     /* the Whisper model an agent's transcriber uses for voice notes */
    char transcribe_languages[64]; /* the languages it writes them in: codes separated by commas, or auto */
    int  transcribe_auto;          /* voice notes other people send are transcribed as they arrive */
    char owner_chat[16];              /* the account whose "message yourself" chat is your chat with the agent, as its id; empty: none */
    int  owner_replies_per_hour;      /* how many answers an agent may send there by itself in an hour */
    int  owner_approvals;             /* waiting requests are put to you there too */
    int  owner_card_wait;             /* seconds a request waits in tawk first */
    char default_agent[64];           /* your default agent, by its label: the one tawk turns to by itself (it writes TL;DR summaries); empty: none chosen */

    /* Advanced */
    char backend[16];           /* whatsmeow (in-process) or baileys (node sidecar) */
    char data_dir[512];
    char sidecar_dir[512];
    char node_binary[256];
    char log_level[16];

    /* Derived at load time, not persisted */
    char config_path[512];
    int  config_trusted;        /* file owned by user and not group/world writable */
} Settings;

/* Fills every field with defaults; paths follow XDG conventions. */
void settings_set_defaults(Settings *settings);
/* Paths derived from data_dir. */
void settings_db_path(const Settings *settings, char *out, unsigned long size);
void settings_auth_dir(const Settings *settings, char *out, unsigned long size);
/* Logs follow XDG state: ~/.local/state/APP_NAME. */
void settings_log_path(const Settings *settings, char *out, unsigned long size);

#endif
