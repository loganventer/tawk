# Configuration

## Table of Contents

- [Where things live](#where-things-live)
- [The config file](#the-config-file)
- [All settings](#all-settings)
- [Themes](#themes)
- [Choosing a backend](#choosing-a-backend)
- [Database encryption (SQLCipher)](#database-encryption-sqlcipher)
- [Audio systems](#audio-systems)
- [Command settings and file trust](#command-settings-and-file-trust)
- [Ctrl+Shift+L in Windows Terminal](#ctrlshiftl-in-windows-terminal)
- [Shift+Enter in Windows Terminal](#shiftenter-in-windows-terminal)
- [Environment variables](#environment-variables)

## Where things live

tawk follows the [XDG Base Directory](https://specifications.freedesktop.org/basedir/latest/) specification for your files and the [Filesystem Hierarchy Standard](https://refspecs.linuxfoundation.org/FHS_3.0/fhs/index.html) for installed files.

Your files (created with owner-only permissions, folders 0700 and files 0600):

| Path | Contents | Safe to delete? |
|---|---|---|
| `~/.config/tawk/config.ini` | Settings | Yes, defaults are written again |
| `~/.config/tawk/themes/` | Your own themes | Yes |
| `~/.local/share/tawk/tawk.db` | Chats, messages, contacts, reactions, per-chat preferences (drafts, mutes, tones, chat themes, soft locks), profiles (about text, business and group details, which picture files belong to whom, the block list) and statuses (kept for `status_keep_days`) | Yes, history syncs again after linking, but local preferences are lost |
| `~/.local/share/tawk/tawk.db.pre-v10` (and later versions) | A copy of the database as it was before tawk upgraded it, kept once so a failed or unwanted upgrade can be undone by hand. Encrypted when the database is. `tawk --encrypt` offers to remove unencrypted ones | Yes, once the upgraded tawk works |
| `~/.local/share/tawk/tawk.lock` | Held while tawk runs, so only one tawk uses the data folder at a time (and `--encrypt`, `--decrypt` and `--change-passphrase` wait for it to quit) | Yes, when no tawk is running |
| `*.before-restore-<date>-<time>` beside the database, config, themes, media and login | What `tawk --restore` replaced, kept in case you want it back | Yes, once the restored data is fine |
| `~/.local/share/tawk/.backup-*`, `.restore-*` | Working folders of a backup or restore, removed when it ends (one is left only if tawk was killed halfway) | Yes, when no backup or restore is running |
| `~/.local/share/tawk/auth/` | The WhatsApp login of the first account | Deleting it unlinks that number |
| `~/.local/share/tawk/accounts/<N>/auth/` | The login of each further account, by its id | Deleting one unlinks that number; its chats stay in the database |
| `~/.cache/tawk/media/` | Downloaded photos, videos, voice notes and documents, with the frames taken from videos (`<video>.poster.jpg`) and the rendered pages of PDFs (`<pdf>.p<N>.png`) beside them, profile pictures (`pic-<hash>-<id>.jpg`, `-full` for the full size), and the photos and videos of statuses (removed with the status once it is older than `status_keep_days`) | Yes, profile pictures are fetched again |
| `~/.local/state/tawk/` | `tawk.log`, `sidecar.log`, `whatsmeow.log` | Yes, or empty them with Settings, Advanced, Clear logs |
| `$XDG_RUNTIME_DIR/tawk/` | `control.sock`, the control socket, while tawk runs with agent access on (0600, in a 0700 folder) | Removed when tawk quits |

Save to Downloads copies a received file out of the media cache into your downloads folder (see `download_dir`). Exporting a chat from its contact details writes `tawk chat with NAME.txt` there, or with media a folder `tawk chat with NAME` holding the text file and copies of the downloaded files. Those copies are yours: tawk creates them with mode 0644, which its private umask (077) narrows to owner only (0600, and 0700 for an export folder); tawk never overwrites a file there (a second export gets ` (1)` added to its name), and it never prunes or deletes them.

Installed files (with the default prefix `/usr/local`):

| Path | Contents |
|---|---|
| `/usr/local/bin/tawk` | The program |
| `/usr/local/share/tawk/themes/` | Bundled themes |
| `/usr/local/share/tawk/sounds/` | Notification sound |
| `/usr/local/share/tawk/emoji/emoji.tsv` | The emoji list used by the emoji picker and `(word` shortcodes (Unicode emoji data with CLDR keywords) |
| `/usr/local/lib/tawk/sidecar/` | The Node.js backend, when installed |
| `/usr/local/share/man/man1/tawk.1` | Man page |
| `/usr/local/share/doc/tawk/` | This documentation and the licence |
| `/usr/local/share/bash-completion/completions/tawk` | Bash completion |

A user install (`--prefix ~/.local`) puts the same tree under `~/.local`. When tawk runs straight from a source checkout it falls back to `themes/`, `assets/emoji/` and `sidecar/` in the current folder.

`tawk --doctor` reports where each of these was found and whether your folders are writable (see [MANUAL.md](MANUAL.md#checking-your-setup)).

## The config file

`~/.config/tawk/config.ini` is a plain INI file with one section per settings menu. tawk writes it on first run with every key and a comment explaining each one, and rewrites it whenever you change something in the settings panel, so the panel and the file always agree. You can also edit it by hand while tawk is closed; unknown keys are ignored and out-of-range numbers are clamped.

Use another file with `tawk --config /path/to/config.ini`.

## All settings

Settings marked "restart required" apply the next time tawk starts. Everything else applies immediately. Numbers in brackets are the allowed range; values outside it are clamped.

Some preferences are set per chat: mutes, pins, archiving, the notification tone, the chat theme, the draft and the soft lock. They are set from the chat options (Alt+O) or with slash commands and are kept in the chat database. The soft lock is local to tawk and never sent to WhatsApp.

### [appearance]

| Key | Default | Description |
|---|---|---|
| `theme` | `whatsapp-dark` | Colour theme; step through the list to preview, Enter to apply. An id that matches no theme (for example a removed file) falls back to `whatsapp-dark` |
| `sidebar_width` | `34` | Chat list width in columns (20 to 80) |
| `chat_list_style` | `detailed` | `detailed` shows the last message under each chat; `compact` shows one line per chat |
| `chat_spacing` | `1` | Blank lines between chats in the list (0 to 2), in both list styles |
| `pinned_folded` | `false` | The Pinned group of the chat list is folded away; kept up to date as you fold it (Enter on its header, or ← and →) and not shown in the settings panel |
| `chats_folded` | `false` | The Chats group (every chat that is not pinned) is folded away; kept up to date in the same way and not shown in the settings panel |
| `sidebar_collapsed` | `false` | Ctrl+B toggles it at any time |
| `use_24h_clock` | `true` | Show 14:05 instead of 2:05 PM |
| `inline_thumbnails` | `true` | Show photos, videos and the first page of PDFs as pictures in the chat; click one to open it |
| `portraits` | `true` | Show contacts' and groups' profile pictures in the chat list (detailed style) and in the chat title bar, which then takes two rows. Without a picture, or without room for one, a coloured badge with the name's initials is shown |
| `image_mode` | `auto` | `sixel` draws real pixels (Windows Terminal 1.22+, WezTerm, foot, mlterm, contour); `blocks` uses coloured half blocks and works in any 256-colour terminal; `auto` picks sixel where it is known to work and not inside tmux or screen. Not shown in the settings panel; set it here |
| `mouse` | `true` | Click chats, scroll, and open media |
| `splash` | `true` | Show the animated logo for about two seconds while tawk connects; any key skips it |

### [chats]

| Key | Default | Description |
|---|---|---|
| `enter_sends` | `true` | When off, Enter adds a new line and Ctrl+S sends. Shift+Enter and Alt+Enter add a new line either way |
| `link_previews` | `false` | Fetch a preview card (title, description, picture) for the first https link in messages you send. The site sees your IP address; addresses on your own network are never fetched. Cards in messages you receive always show and fetch nothing |
| `format_text` | `true` | Show WhatsApp's formatting (`*bold*`, `_italic_`, `~strikethrough~`, `` `code` ``, blocks, quotes, lists and mentions) as the phone does; `false` shows the marks as typed |
| `convert_emoticons` | `true` | Turn emoticons such as `:)`, `<3`, `:D`, `;)` and `:P`, and shortcodes such as `:fire:` and `:tada:`, into emoji when you type a space or press Enter. Only a whole word is converted, so links are left alone. Also offers emoji for a word typed after a bracket, such as `(hu`, and replaces a closed `(pizza)` that fits only one |
| `message_margin` | `50` | How many messages the chat view keeps in memory either side of what is on screen (20 to 500). The window slides as you scroll, so a long chat opens as fast as a short one |
| `share_typing` | `true` | Show "typing…" (or recording audio) to the other person while you type or record |
| `appear_online` | `true` | Show as online while tawk is in use; needed to see others typing and online |
| `show_online` | `true` | Show "online" or "last seen" under the name of the open chat, for people who share it with you. Needs `appear_online` |
| `show_transcripts` | `true` | Show the words of a voice note under it when it has been transcribed. A chat can say otherwise on its contact card (always or never) |
| `transcript_lines` | `6` | Lines of a transcript shown in the conversation, 1 to 40. The message menu's "Show transcript" shows the rest |
| `tldr_min_chars` | `300` | In a chat with TL;DR switched on (on its contact card), a message at least this many characters long shows as a summary. 100 to 5000 |
| `tldr_back_days` | `30` | How many days back a TL;DR chat's older long messages are summarised by themselves, newest first. 0 to 365; 0 summarises only what you look at |
| `reopen_last_chat` | `true` | When tawk starts, open the chat that was open when it last quit. Skipped when that chat was deleted or is in Locked chats |
| `last_chat` | empty | The chat open when tawk last ran; kept up to date as you open chats and not shown in the settings panel |
| `recent_emoji` | `👍 ❤️ 😂 😮 😢 🙏` | Space-separated, most recent first; kept up to date by the emoji picker (up to 24) and not shown in the settings panel |
| `status_keep_days` | `30` | How long tawk keeps statuses (1 to 365 days). WhatsApp shows a status for a day; after that it moves to the Status archive until this many days have passed. 1 keeps no archive. While the archive is on and `auto_download` is set, a status's photo or video is fetched when it arrives (up to `auto_download_max_mb`), since WhatsApp's copy does not last |
| `send_read_receipts` | `true` | Tell senders when you have read their messages |

### [notifications]

| Key | Default | Description |
|---|---|---|
| `enabled` | `true` | Master switch for all alerts |
| `do_not_disturb` | `false` | Silence everything, a ringing call included; Ctrl+D toggles it |
| `group_notifications` | `true` | Alert for group messages |
| `mention_notifications` | `true` | Being @mentioned alerts you even in a muted chat or with group alerts off (never during do not disturb) |
| `show_preview` | `true` | Include message text in the title bar |
| `sound` | `true` | Play a sound for new messages, and every 3 seconds while a call rings |
| `sound_file` | `/usr/local/share/tawk/sounds/notify.wav` | WAV/OGG file to play |
| `blink` | `true` | Blink the chat in the list and the status bar |
| `blink_seconds` | `8` | How long the blink lasts (1 to 60) |
| `title_flash` | `true` | Show per-type unread counts in the terminal title and flash it |
| `terminal_bell` | `false` | Ring the bell; Windows Terminal flashes the taskbar |
| `screen_flash` | `false` | Flash the whole screen once |

### [media]

| Key | Default | Description |
|---|---|---|
| `auto_download` | `true` | Fetch photos and videos when they arrive |
| `auto_download_max_mb` | `16` | Larger files download when clicked (1 to 512) |
| `image_viewer` | `(empty)` | Empty or `builtin` shows photos, videos and PDFs in the viewer inside tawk; `system` uses the system default viewer; anything else is a command such as `eog` |
| `video_player` | `(empty)` | Empty tries mpv, vlc, celluloid, totem and ffplay (which comes with ffmpeg), then the system default; or a command |
| `download_dir` | `(empty)` | Where Save to Downloads puts files. Empty uses your downloads folder: `XDG_DOWNLOAD_DIR` from the environment or from `~/.config/user-dirs.dirs`, else `~/Downloads`. A leading `~/` is expanded, and the folder is created when missing |
| `attach_dir` | `(empty)` | The folder the file picker opens in; tawk keeps it set to the last folder you attached from (your home folder when empty or missing) |
| `media_cache_mb` | `1024` | Oldest downloads are removed above this size (50 to 100000) |
| `media_dir` | `~/.cache/tawk/media` | Downloaded media (a cache: safe to delete) (restart required) |
| `audio_backend` | `auto` | auto detects PulseAudio, PipeWire, ALSA, CoreAudio or DirectShow |
| `mic_device` | `default` | Capture device for the audio system; "default" uses the system default |
| `voice_max_seconds` | `300` | Recording stops and sends at this length (10 to 900) |

### [screensaver]

| Key | Default | Description |
|---|---|---|
| `enabled` | `true` | Run a command after a period of inactivity |
| `idle_minutes` | `5` | Inactivity before the screensaver starts (1 to 240) |
| `command` | `matrix-clock` | Shell command to run; any key stops it. Ctrl+L, `/screensaver` and `/lock` start it at once |
| `wake_on_message` | `true` | Stop the screensaver when a message arrives |

### [resilience]

| Key | Default | Description |
|---|---|---|
| `backoff_initial_ms` | `1000` | First reconnect delay; doubles each attempt (100 to 60000) |
| `backoff_max_ms` | `60000` | Upper bound for the exponential backoff (1000 to 600000) |
| `breaker_threshold` | `5` | Consecutive failures before retries pause (1 to 50) |
| `breaker_cooldown_s` | `120` | Pause before a trial reconnect (5 to 3600) |

### [automation]

What other programs reaching tawk through its control socket may do: tawk-mcp and the `tawk send`, `tail`, `unread` and `status-line` commands. See [Automation and MCP](MANUAL.md#automation-and-mcp) and [CONTROL.md](CONTROL.md).

| Key | Default | Description |
|---|---|---|
| `control_socket` | `off` | Listen on the control socket. Turning it on in the settings panel explains the risks first |
| `access` | `read` | `read`, `send`, `manage` or `admin`: see the manual. Programs acting for a model are asked about every write; with `admin`, one holding the admin token may answer its own sends in the chats in `self_approval_chats` |
| `chats` | empty | Chats they may use, by name or number, comma-separated; empty means every chat except locked and soft-locked ones |
| `confirm_cli` | `off` | Your own shell commands ask before sending too |
| `writes_per_minute` | `5` | Writes allowed per minute (1 to 60) |
| `ai_disclaimer` | `off` | Add a line under messages a program acting for a model sends, schedules or answers a status with, saying an AI wrote them |
| `ai_disclaimer_text` | `🤖 Created with my AI assistant` | That line |
| `push_received` | `on` | Programs acting for a model that subscribed are told about each message other people send, as it arrives. Off, they see messages only when they read a chat |
| `push_sent` | `on` | The same for the messages you send. Your own shell commands (`tawk tail`) always hear both |
| `push_read` | `off` | Programs acting for a model that subscribed are told when someone reads a message you sent |
| `push_reactions` | `off` | The same when someone reacts to a message you sent, or takes a reaction back |
| `push_edits` | `off` | The same when someone changes or deletes a message they sent |
| `push_scheduled` | `off` | The same when a message you scheduled goes out |
| `push_presence` | `off` | The same when the person in a chat you have opened comes online or leaves. Only for chats the program may use, and only for a chat you opened or one it looked up |
| `presence_lookup` | `off` | A program acting for a model may ask whether the person in a chat is online or when they were last seen, one person at a time and only for the chats it may use. Off, it cannot ask. Your own shell always may |
| `self_approval_chats` | empty | With `access = admin`: the chats a program may answer its own sends in, as JIDs separated by commas, or `*` for every chat it may use. Empty allows none. Set from Settings, Automation, Answering for itself, where each chat has a switch |
| `transcribe_model` | `large-v3-turbo` | The Whisper model an agent's transcriber (tawk-mcp) uses for voice notes: `tiny`, `base`, `small`, `medium`, `large-v3-turbo` or `large-v3`. A list to choose from in the settings panel |
| `transcribe_languages` | `auto` | The languages voice notes are written out in, as codes separated by commas (`af,en`), or `auto`. Each one gets its own transcription |
| `transcribe_auto` | `off` | Every voice note other people send is transcribed as it arrives. Off, only the ones an agent asks for |
| `default_agent` | (empty) | Your default agent, by its label without the process id: the agent tawk turns to by itself, which writes TL;DR summaries. Set with **d** in the Agents list or by answering tawk's question on WhatsApp. Empty uses the only agent connected, or asks when there are several |
| `self_approvals_per_hour` | `20` | With `access = admin`: requests a program may answer itself in an hour (1 to 240); past this they wait for you |

None of these can be changed over the socket, nor can settings that run a program, folders, the backend or the log level.

### [advanced]

| Key | Default | Description |
|---|---|---|
| `backend` | `whatsmeow` | whatsmeow runs in-process; baileys runs a Node.js sidecar (restart required). Posting statuses needs whatsmeow |
| `data_dir` | `~/.local/share/tawk` | Chat database and WhatsApp login (restart required) |
| `sidecar_dir` | `/usr/local/lib/tawk/sidecar` | Location of the WhatsApp bridge (restart required) |
| `node_binary` | `node` | Runtime used for the bridge (restart required) |
| `log_level` | `info` | debug, info, warn or error (restart required) |

Settings, Advanced also has a Clear logs action. It empties `tawk.log` and every other `*.log` file in the same folder (the backend's log included) in place, so the programs writing them carry on, and reports how much space it freed.

### Settings and accounts

The config file holds one set of settings for tawk as a whole. What belongs to one account is kept in the database, in the `accounts` table, and is changed under Settings, Account, Accounts…: its label, whether it is the primary account, what agents may do with it, and the chats an agent may answer by itself in.

- `merge_accounts` (`[chats]`, default `true`): someone who writes to several of your numbers shows as one chat. A contact can be set apart, or always merged, on its contact card.
- `[automation] access` is the level an account uses while its own level is *follow*. Only the first account starts at *follow*; every account you add starts at *off*.
- `[automation] self_approval_chats` is still read for an account at *follow* that has no list of its own, so an existing setup keeps working. A chat switched on or off from a contact card is stored with its account.
- `[automation] chats`, the rate limits and the AI disclaimer apply to every account.

## Themes

A theme is a JSON file. tawk loads the bundled folder first and then `~/.config/tawk/themes/`; a theme of yours with the same `id` replaces the bundled one. The picker lists themes by name.

```json
{
  "id": "my-theme",
  "name": "My Theme",
  "description": "Shown next to the name in the picker",
  "colors": {
    "base":             { "fg": "#e9edef", "bg": "#111b21" },
    "header":           { "fg": "#e9edef", "bg": "#202c33" },
    "sidebar":          { "fg": "#e9edef", "bg": "#111b21" },
    "sidebar_selected": { "fg": "#e9edef", "bg": "#2a3942" },
    "unread":           { "fg": "#00a884", "bg": "#202c33" },
    "badge":            { "fg": "#111b21", "bg": "#00a884" },
    "chat":             { "fg": "#e9edef", "bg": "#111b21" },
    "bubble_me":        { "fg": "#e9edef", "bg": "#005c4b" },
    "bubble_them":      { "fg": "#e9edef", "bg": "#2a3942" },
    "sender":           { "fg": "#53bdeb", "bg": "#2a3942" },
    "timestamp":        { "fg": "#8696a0", "bg": "#111b21" },
    "day_separator":    { "fg": "#8696a0", "bg": "#2a3942" },
    "composer":         { "fg": "#e9edef", "bg": "#202c33" },
    "accent":           { "fg": "#00a884", "bg": "#111b21" },
    "ok":               { "fg": "#00a884", "bg": "#202c33" },
    "warn":             { "fg": "#ffb02e", "bg": "#202c33" },
    "blink":            { "fg": "#111b21", "bg": "#00a884" },
    "media":            { "fg": "#53bdeb", "bg": "#111b21" },
    "dim":              { "fg": "#8696a0", "bg": "#111b21" },
    "border":           { "fg": "#2a3942", "bg": "#111b21" }
  }
}
```

- Colours can be `#RRGGBB`, `#RGB`, an xterm-256 index (`0` to `255`) or `"default"` for the terminal's own colour.
- Hex colours are matched to the nearest of the terminal's 256 colours. On an 8-colour terminal they fall back to the nearest basic colour.
- Every slot is optional. A missing slot borrows a sensible neighbour (for example `bubble_me` borrows `header`), so a small file with `base`, `header` and `accent` already works.
- Files over 64 KB or with invalid JSON are skipped and noted in the log.

After adding or editing a file, use Settings, Appearance, Theme, Reload theme files. If the theme named in `config.ini` is no longer found, tawk uses the default theme, `whatsapp-dark`, rather than the first theme in the list.

A theme can also be applied to a single chat with its chat options or `/theme <id>`. Only the conversation pane (bubbles, the chat title bar, day separators and the meta line) takes the chat's colours; the chat list, header, input and footer keep the app theme. The chat stores the theme's `id`, so a personal theme with the same `id` as a bundled one is used there too.

## Database encryption (SQLCipher)

`tawk --encrypt` encrypts the database with a passphrase (see [the manual](MANUAL.md#encrypting-your-chats)). It needs a build with SQLCipher, a copy of SQLite that can encrypt. The build uses SQLCipher instead of SQLite whenever `pkg-config` finds it, and the configure summary says which one it chose:

```
  sqlite      SQLCipher 3.51.2, database encryption available
```

The installer adds it (`libsqlcipher-dev` on Debian and Ubuntu, `sqlcipher-devel` on Fedora and openSUSE, `sqlcipher` on Arch and Homebrew). `make SQLCIPHER=0` builds with plain SQLite even when SQLCipher is installed; such a build cannot open an encrypted database and says so. The passphrase is never a setting: it is typed at start-up and kept only in memory.

Backups (`tawk --backup`, see [the manual](MANUAL.md#backups)) need no build option: they are packed with the system's `tar` and encrypted with the `openssl` command, which the installer adds.

## Choosing a backend

| | whatsmeow (default) | Baileys |
|---|---|---|
| Runs | Inside the tawk process | As a Node.js child process |
| Needs at runtime | Nothing | Node.js 20 or newer |
| Needs to build | Go 1.21 or newer | npm |
| Start-up | Instant | A few seconds (Node.js start and module loading) |
| If the protocol layer crashes | tawk exits | tawk restarts the sidecar with backoff |

Each backend keeps its own login (whatsmeow in `~/.local/share/tawk/auth/whatsmeow.db`, Baileys in `~/.local/share/tawk/auth/baileys/`), so switching backends means linking once more, and logging out of one never affects the other. Set `backend` in `[advanced]` (Settings, Connection, Backend) or start once with `tawk --backend baileys`.

Posting a status needs whatsmeow; viewing statuses and changing your profile work on both. On Baileys, + in the header and `/status` offer to switch: tawk saves `backend = whatsmeow`, shuts down cleanly and starts again without any `--backend` option, ready to link with a QR code or pairing code. Your chats stay in the database.

## Audio systems

`audio_backend = auto` picks the first available of CoreAudio (macOS), DirectShow (Windows builds), PipeWire, PulseAudio and ALSA. Under WSL this is PulseAudio through WSLg.

| Backend | Records with | Plays with |
|---|---|---|
| `pulse` | `parecord` | `ffplay`, else `paplay` |
| `pipewire` | `pw-record` | `pw-play` |
| `alsa` | `arecord` | `ffplay`, else `aplay` |
| `coreaudio` | `ffmpeg -f avfoundation` | `ffplay`, else `afplay` |
| `dshow` | `ffmpeg -f dshow` | `ffplay` |

Recordings are always encoded by `ffmpeg` to 48 kHz mono Ogg/Opus, the format WhatsApp uses for voice notes. `mic_device` takes the device name your audio system uses (`pactl list short sources`, `pw-cli ls Node`, `arecord -L`, or `ffmpeg -f avfoundation -list_devices true -i ""`). DirectShow needs the exact device name.

## Command settings and file trust

Four settings name programs tawk will start: `screensaver.command`, `media.image_viewer`, `media.video_player` and `advanced.node_binary` (plus `advanced.sidecar_dir`, which decides what code Node.js runs). tawk honours them only when `config.ini` is owned by you and not writable by your group or others. If the file is shared or loosely permissioned, tawk logs a warning, uses the defaults for those settings, and does not rewrite the file. Fix it with:

```bash
chmod 600 ~/.config/tawk/config.ini
```

`tawk --doctor` warns when the file is not private. While it is not, Ctrl+L, `/screensaver` and the idle timer show a message instead of starting the screensaver.

The screensaver command runs through `/bin/sh -c` so that pipes and arguments work. Viewer and player settings are split into arguments without any shell, and the file path is passed as a separate argument.

## Ctrl+Shift+L in Windows Terminal

Ctrl+Shift+L soft-locks the selected or open chat. To tell it apart from Ctrl+L (the screensaver), tawk asks the terminal to report modified keys in the xterm "modifyOtherKeys" form (`CSI > 4;1 m` at start-up, turned off again on exit) and recognises both `CSI 27;<mods>;<key>~` and `CSI <key>;<mods> u`. Terminals that do not report the combination distinctly send plain Ctrl+L instead. Alt+L, `/softlock` and the chat options do the same job in any terminal.

Windows Terminal does not report Ctrl+Shift+L distinctly. To make it work there, open Settings, Open JSON file, and add this entry to the `actions` list, so the terminal sends the sequence tawk recognises:

```json
{ "command": { "action": "sendInput", "input": "\u001b[27;6;76~" }, "keys": "ctrl+shift+l" }
```

The binding applies to every program in Windows Terminal, so other programs that read Ctrl+Shift+L see this sequence too.

## Shift+Enter in Windows Terminal

Shift+Enter starts a new line in the message input and the status composer, and so does Alt+Enter in every terminal. Terminals that report modified keys (xterm, WezTerm, foot, Ghostty, iTerm2) send Shift+Enter distinctly with no setup. Windows Terminal sends it exactly like Enter, so it sends the message. To make it a new line there, add this entry to the `actions` list as well:

```json
{ "command": { "action": "sendInput", "input": "\u001b[13;2u" }, "keys": "shift+enter" }
```

Like the Ctrl+Shift+L binding, it applies to every program in Windows Terminal.

## Environment variables

| Variable | Effect |
|---|---|
| `XDG_CONFIG_HOME` | Moves `~/.config` |
| `XDG_DATA_HOME` | Moves `~/.local/share` |
| `XDG_CACHE_HOME` | Moves `~/.cache` |
| `XDG_STATE_HOME` | Moves `~/.local/state` |
| `XDG_RUNTIME_DIR` | Where the control socket goes (`$XDG_RUNTIME_DIR/tawk/control.sock`); without it, the state folder |
| `TAWK_CONTROL_SOCKET` | An absolute path for the control socket instead, for tawk and its shell commands alike |
| `XDG_DOWNLOAD_DIR` | Where Save to Downloads puts files when `download_dir` is empty; an absolute path. Without it tawk reads `~/.config/user-dirs.dirs`, then uses `~/Downloads` |
| `WT_SESSION` | Set by Windows Terminal; enables the tab progress ring |
| `TERM` | Should describe a 256-colour terminal such as `xterm-256color` for full theme colours and photo previews |
| `COLORTERM` | Set by true-colour terminals; `tawk --doctor` uses it when checking colour support |
| `LANG`, `LC_ALL` | Should select a UTF-8 locale so emoji and borders draw correctly |
