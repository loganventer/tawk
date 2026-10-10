# Quickstart

Having an AI agent install tawk for you? Point it at [AGENT_SETUP.md](AGENT_SETUP.md), which covers Windows, Linux and macOS step by step.

## Install with one command

```bash
curl -fsSL https://raw.githubusercontent.com/loganventer/tawk/main/install.sh | bash
```

The script detects your platform, offers to install the build dependencies and the runtime tools (it asks first), downloads the source into a private temporary folder, builds it, installs it to `/usr/local` and removes the temporary folder. It uses `sudo` only for the final copy when `/usr/local` is not writable. At the end it runs `tawk --doctor` to confirm everything tawk needs is in place.

The runtime tools are only added when they are missing:

| Package manager | Voice notes | Recording tools | Opening media | PDF pages | Pasting pictures |
|---|---|---|---|---|---|
| apt (Debian, Ubuntu) | `ffmpeg` | `pulseaudio-utils` | `xdg-utils` | `poppler-utils` | `wl-clipboard`, `xclip` |
| dnf (Fedora) | `ffmpeg-free` | `pulseaudio-utils` | `xdg-utils` | `poppler-utils` | `wl-clipboard`, `xclip` |
| pacman (Arch) | `ffmpeg` | `libpulse` | `xdg-utils` | `poppler` | `wl-clipboard`, `xclip` |
| zypper (openSUSE) | `ffmpeg` | `pulseaudio-utils` | `xdg-utils` | `poppler-tools` | `wl-clipboard`, `xclip` |
| Homebrew (macOS) | `ffmpeg` | `sox` (CoreAudio) | (`open`, built in) | `poppler` | `pngpaste` |

The recording tools are skipped when `parecord`, `pw-record` or `arecord` is already installed (`rec` on macOS), the media opener when `xdg-open` is, the PDF tools when `pdftoppm` is, and each clipboard tool when `wl-paste`, `xclip` or `pngpaste` is. The script's toolchain summary lists `pdftoppm`, `wl-paste` and `xclip` with the other tools.

Prefer to read the script before running it? Download it, look, then run it:

```bash
curl -fsSLO https://raw.githubusercontent.com/loganventer/tawk/main/install.sh
less install.sh
bash install.sh
```

## Install from a checkout

```bash
git clone https://github.com/loganventer/tawk.git
cd tawk
./install.sh
```

## Common variations

| Goal | Command |
|---|---|
| Install for your user only, no sudo | `./install.sh --prefix "$HOME/.local"` |
| Also install the Node.js (Baileys) backend | `./install.sh --with-sidecar` |
| No Go available | `./install.sh --no-whatsmeow` |
| Build a tag or branch | `./install.sh --ref v0.2.0` |
| Add a short alias such as `wa` | `./install.sh --alias wa` |
| Skip package installation | `./install.sh --no-deps` |
| Install packages without asking | `./install.sh --yes` |
| Remove tawk | `./install.sh --uninstall` |

Everything the script does can be done by hand with `make`, see the Building section of the [README](README.md).

## First run

```bash
tawk
```

![The linking wizard showing a QR code and the steps on the phone](docs/images/login-qr.png)

1. **Link your phone.** A wizard opens. Choose **Scan a QR code** and, on your phone, open WhatsApp, go to **Settings** (iPhone) or the **⋮** menu (Android), then **Linked devices**, **Link a device**, and point the camera at the terminal. Or choose **Use my phone number**, type your number with its country code (for example `27821234567`), press Enter, and type the 8-character code on your phone under **Link with phone number instead**.
2. **Wait for the sync.** tawk shows "Linked" and fills the chat list as WhatsApp sends your recent history.
3. **Chat.** Use the arrow keys and Enter to open a chat, type, and press Enter to send.

![tawk after linking: the chat list on the left and a conversation on the right](docs/images/main.png)

The QR code needs a window of roughly 70 by 40 characters. If yours is smaller, tawk tells you, or you can use the phone number instead.

If something does not work, run `tawk --doctor`. It checks the terminal, your files, the backend, the voice note and media tools, the PDF page tools and the screensaver command, and tells you what to install.

![tawk --doctor listing each check and what to install for the one that is missing](docs/images/doctor.png)

## Tips

- Press **F2** (or click **⚙** in the top right) for settings. Changes are saved as you make them.
- Type **/help** in the input for every slash command and the main keys.
- **Ctrl+K** searches every chat; **Ctrl+E** opens the emoji picker.
- On a message, **Alt+Q** replies, **Alt+E** reacts and **Alt+M** opens the message menu (or right-click it). Typing anywhere else writes in the input; typing in the chat list searches it.
- Click a photo, video or PDF to see it full size inside tawk; ← → browse (and turn the pages of a PDF), ↑ ↓ move to other media, Esc closes.
- **Delete** on a selected message deletes it for you or, for your own recent messages, for everyone. The message menu also has **Save to Downloads**.
- **Alt+O** opens a chat's options: mute, pin, archive, soft lock, its own theme or notification tone.
- Click your name in the header (or type **/profile**) to change your name, about text or photo.
- **⭕** in the header (or **/statuses**) shows your contacts' statuses; the number next to it counts people with statuses you have not seen. **+** left of the clock (or **/status**) posts your own: text, photo, video or link. Posting needs the whatsmeow backend.
- Click a chat's name in the title bar (or press **Alt+I**, or type **/info**) for its details: about text, group members, block, clear, delete or export the chat. Click its picture to see it full size.
- **Alt+L** (Option+L on a Mac, or **Ctrl+Shift+L** where the terminal reports it) soft-locks a chat: its conversation is blurred until you press it again.
- **Shift+Enter** (or Alt+Enter) starts a new line in a message; Enter sends it. Ctrl, Alt or Shift with ← and → steps through what you typed a word at a time.
- Pinned chats have their own group at the top of the list. Enter on a group header, or ← and →, folds and unfolds it.
- **Ctrl+R** records a voice note in the open chat; Enter sends it.
- With an agent connected through [tawk-mcp](https://github.com/loganventer/tawk-mcp), voice notes show their words under them (**Alt+T** steps this for the open chat), and a chat's contact card has **TL;DR** to show long messages as a short summary; Enter unfolds one.
- Emoticons such as `:)` and `<3` become emoji when you type a space, and `(pizza)` becomes 🍕; type `(hu` to pick from matching emoji. Turn this off under Settings, Chats, Emoticons to emoji.
- When someone calls, tawk rings with a prompt: **d** declines, Enter or Esc closes it so you can answer on your phone. tawk cannot carry the call itself.
- Drag a file from your file manager onto the terminal window to send it, or press **Ctrl+O** to pick one.
- Click **➕** in the input and choose Take a photo (or type **/camera**) for the camera: Space takes a photo, V records a video, and Enter attaches what you took.
- **Ctrl+B** hides the chat list when the window is narrow.
- Try a theme: F2, Appearance, Theme, Choose theme, then use the arrow keys to preview.
- Everything else is in the [MANUAL](MANUAL.md).
