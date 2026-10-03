# Setting up tawk: instructions for an agent

Read this when a person asks you, an AI agent, to install tawk for them. It covers Windows, Linux and macOS from nothing to a linked, running tawk. A person installing by hand should read [QUICKSTART.md](QUICKSTART.md) instead.

To add the agent side afterwards (reading chats and proposing messages through MCP), carry on with tawk-mcp's [AGENT_SETUP.md](https://github.com/loganventer/tawk-mcp/blob/main/AGENT_SETUP.md).

## Table of Contents

- [What you are installing](#what-you-are-installing)
- [Rules for the agent](#rules-for-the-agent)
- [Before you start](#before-you-start)
- [Windows](#windows)
- [Linux](#linux)
- [macOS](#macos)
- [Check the install](#check-the-install)
- [First run and linking](#first-run-and-linking)
- [Getting ready for tawk-mcp](#getting-ready-for-tawk-mcp)
- [Updating and removing](#updating-and-removing)
- [When something goes wrong](#when-something-goes-wrong)
- [What to tell the person at the end](#what-to-tell-the-person-at-the-end)

## What you are installing

tawk is a WhatsApp client that runs in a terminal. It is one C program built from source on the person's own machine, and it links to their phone as a linked device, the same way WhatsApp Web does. Chats are kept in a SQLite file on that machine. There is no account to create and nothing to pay for.

tawk is an independent project, not affiliated with or endorsed by WhatsApp or Meta, and it uses unofficial protocol libraries. Say so to the person before you install it, so that using it, and in line with WhatsApp's terms of service, is their own decision. The linked session in `~/.local/share/tawk/` gives access to their account, and they can end it at any time on the phone under **Linked devices**.

| Fact | Value |
|---|---|
| Source | `https://github.com/loganventer/tawk` (public, MIT) |
| Installer | `https://raw.githubusercontent.com/loganventer/tawk/main/install.sh` |
| Installs to | `/usr/local/bin/tawk` by default, or `PREFIX/bin/tawk` with `--prefix` |
| Settings | `~/.config/tawk/config.ini` |
| Chats and login | `~/.local/share/tawk/` |
| Platforms | Linux and macOS natively; Windows through WSL |

## Rules for the agent

1. **Ask before anything that changes the system.** The installer adds packages and, by default, copies into `/usr/local` with `sudo`. Say what will be installed and get a yes first. Pass `--yes` to the installer only after the person has agreed.
2. **You cannot type a password for the person.** When `sudo` asks for one, the person types it. If you cannot hand them the prompt, install without `sudo` using `--prefix "$HOME/.local"`, and ask them to install the system packages themselves.
3. **You cannot link WhatsApp.** Linking needs the person's phone in their hand. Your work ends at a tawk that passes `tawk --doctor`; the person then starts it and links it.
4. **Do not start `tawk` yourself in a shell without a terminal.** It is a full-screen program for a person to use. Everything you need to check is in `tawk --version` and `tawk --doctor`, which need no terminal.
5. **Never read, copy or send the person's chats, login files or database** as part of setting up. Nothing in this guide needs them.
6. **Run only the commands in this guide** unless the person asks for more. When a step fails, show the person the error text as it is, then use the table at the end.

## Before you start

Find out where you are running:

```bash
uname -s                                   # Linux, Darwin, or MINGW/MSYS/CYGWIN on Windows
grep -qiE 'microsoft|wsl' /proc/sys/kernel/osrelease 2>/dev/null && echo "inside WSL"
```

| You see | Follow |
|---|---|
| `Linux`, and "inside WSL" | [Linux](#linux), inside that WSL system. WSL is already there |
| `Linux` | [Linux](#linux) |
| `Darwin` | [macOS](#macos) |
| `MINGW`, `MSYS`, `CYGWIN`, or a PowerShell or cmd prompt | [Windows](#windows) |

The machine needs:

| Need | Why |
|---|---|
| Internet | The installer downloads the source and packages |
| About 600 MB free in the temporary folder | The download and the build. The installer checks and says so when there is too little |
| About 2.5 GB of free memory and swap | Compiling the in-process WhatsApp backend. With less, the installer warns; see [When something goes wrong](#when-something-goes-wrong) |
| `curl`, `git` and `bash` | To fetch and run the installer |
| A terminal window of at least 70 by 40 characters | To show the QR code when linking. Smaller windows can link with a phone number instead |

## Windows

tawk runs on Windows inside WSL (the Windows Subsystem for Linux), shown in Windows Terminal. Everything after step 2 happens inside WSL and is the same as on Linux.

1. **Install WSL with Ubuntu.** This needs an administrator PowerShell, so ask the person to open one (Start, type `PowerShell`, right-click, **Run as administrator**) and run:

   ```powershell
   wsl --install -d Ubuntu
   ```

   Windows may ask to restart. Check whether WSL is already there first with `wsl -l -v`; if a distribution is listed, skip this step.
2. **First start of Ubuntu.** The person opens **Ubuntu** from the Start menu or Windows Terminal and makes up a Linux username and password when asked. That password is the one `sudo` asks for later. You cannot do this step for them.
3. **Run the Linux steps inside WSL.** From a Windows-side shell you reach WSL like this:

   ```powershell
   wsl -e bash -lc "uname -a"
   ```

   Then follow [Linux](#linux), running each command inside WSL.
4. **Use Windows Terminal 1.22 or later.** It draws photos and PDFs as real pixels and shows tawk's status in the tab title. It is in the Microsoft Store, and Windows 11 includes it.

Notes for Windows:

- Voice notes use the microphone through WSLg, which comes with current WSL.
- Photos, videos and files open in Windows applications when WSL may start Windows programs, which is the default. `tawk --doctor` says when it is off.
- The person starts tawk by opening Ubuntu and typing `tawk`.

## Linux

The installer knows `apt` (Debian, Ubuntu), `dnf` (Fedora), `pacman` (Arch) and `zypper` (openSUSE). It installs the compiler, `make`, `pkg-config`, wide-character ncurses, SQLite, Go, and the tools for voice notes, opening media, PDF pages and pasting pictures, each only when it is missing.

1. Make sure `curl` and `git` are there. On Debian or Ubuntu:

   ```bash
   sudo apt-get update && sudo apt-get install -y curl git
   ```

2. Run the installer. Pick one line:

   ```bash
   # The usual way: installs to /usr/local, asks before adding packages
   curl -fsSL https://raw.githubusercontent.com/loganventer/tawk/main/install.sh | bash

   # After the person has agreed to the packages: no questions
   curl -fsSL https://raw.githubusercontent.com/loganventer/tawk/main/install.sh | bash -s -- --yes

   # For this user only, with no sudo for the final copy
   curl -fsSL https://raw.githubusercontent.com/loganventer/tawk/main/install.sh | bash -s -- --prefix "$HOME/.local"
   ```

   Installing packages still needs `sudo`. With `--prefix "$HOME/.local"`, make sure `~/.local/bin` is on `PATH`.
3. Go to [Check the install](#check-the-install).

The build takes a few minutes. Most of that is the WhatsApp backend, written in Go.

## macOS

1. **The Xcode command line tools**, which provide the C compiler and `make`. This opens a window the person must click through:

   ```bash
   xcode-select -p >/dev/null 2>&1 || xcode-select --install
   ```

2. **Homebrew.** Check with `command -v brew`. On Apple silicon it lives in `/opt/homebrew/bin`, which may not be on `PATH` yet, so check there too. If it is missing, the person installs it from [brew.sh](https://brew.sh); it asks for their password.
3. **Run the installer.** It uses Homebrew for everything tawk needs (ncurses, SQLite, pkg-config, Go, ffmpeg and SoX for voice notes, poppler for PDF pages, pngpaste for pasting pictures) and asks before installing anything:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/loganventer/tawk/main/install.sh | bash
   ```

   To install with no `sudo`, put it in the home folder and make sure `~/.local/bin` is on `PATH`:

   ```bash
   curl -fsSL https://raw.githubusercontent.com/loganventer/tawk/main/install.sh | bash -s -- --prefix "$HOME/.local"
   ```

4. **Recommend a terminal.** tawk runs in the macOS Terminal app but looks broken there: thin lines cut through the chat bubbles, and photos show as coloured blocks. Suggest [iTerm2](https://iterm2.com) 3.5 or later, [WezTerm](https://wezterm.org) or [Ghostty](https://ghostty.org).

Notes for macOS:

- macOS asks for microphone access for the terminal the first time a voice note is recorded. The person allows it.
- Alt shortcuts use the Option key; see "Alt shortcuts on a Mac" in [MANUAL.md](MANUAL.md).

## Check the install

Both commands work without a terminal and without WhatsApp:

```bash
tawk --version      # the version and the commit it was built from
tawk --doctor       # every check tawk needs, and what to install for any that fail
```

The installer runs `tawk --doctor` itself at the end. Read its output: each line is a check, and a failed one says what to install. A missing optional tool (clipboard pictures, PDF pages) does not stop tawk from working; say which features it affects and let the person decide.

If the shell says `tawk: command not found`, the folder it was installed into is not on `PATH`. For `--prefix "$HOME/.local"` that is `~/.local/bin`.

## First run and linking

This part is the person's. Tell them:

1. Open a terminal (Ubuntu on Windows), make the window large, and type `tawk`.
2. A wizard opens. Choose **Scan a QR code**. On the phone open WhatsApp, go to **Settings** (iPhone) or the **⋮** menu (Android), then **Linked devices**, **Link a device**, and point the camera at the screen.
3. If the window is too small for the QR code, or a camera is not handy, choose **Use my phone number** instead: type the number with its country code (for example `27821234567`), press Enter, and type the 8-character code on the phone under **Link with phone number instead**.
4. tawk shows "Linked" and fills the chat list as WhatsApp sends recent history.

Then: arrow keys and Enter open a chat, typing and Enter sends, **F2** opens settings, and `/help` lists every command.

## Getting ready for tawk-mcp

Skip this unless the person wants an AI agent to work with their WhatsApp.

An agent reaches tawk through its control socket, which is off by default. The person turns it on in **Settings > Automation > Control socket**, and chooses what agents may do under **Agent access**. `read` is the starting point: an agent can then read and nothing else.

The same in `~/.config/tawk/config.ini`, changed while tawk is closed:

```ini
[automation]
control_socket = on
access = read
```

Do not raise `access` above what the person asked for. `send` lets an agent propose messages that the person approves one by one; `manage` adds changes such as edits and downloads; `admin` lets an agent holding the admin token answer its own sends in chats the person switches on. Each level is the person's decision.

tawk must be running for an agent to reach it. Then follow tawk-mcp's [AGENT_SETUP.md](https://github.com/loganventer/tawk-mcp/blob/main/AGENT_SETUP.md).

## Updating and removing

```bash
tawk --update            # compares this build with the newest on GitHub, asks, then rebuilds
tawk --update --yes      # the same without asking
tawk --reinstall         # installs the newest again even when up to date
```

Close tawk before updating, and start it again afterwards.

To remove tawk, run the installer with `--uninstall`. The person's chats, settings and login are kept:

```bash
curl -fsSL https://raw.githubusercontent.com/loganventer/tawk/main/install.sh | bash -s -- --uninstall
```

## When something goes wrong

| You see | Do this |
|---|---|
| "not enough free disk space to download and build tawk" | Free about 600 MB, or name a folder on a disk with room: `TMPDIR=/path/with/space` in front of the install command |
| The compiler is killed during the build, or a warning about memory and swap | Add swap as the installer suggests, or install with `--no-whatsmeow` to use the Node.js backend instead (needs Node.js 20 or newer) |
| `tawk: command not found` after installing | Add the install folder's `bin` to `PATH`, or open a new terminal |
| `sudo` asks for a password and you cannot answer | The person types it, or install with `--prefix "$HOME/.local"` |
| On macOS, "Homebrew is not installed" | The person installs it from brew.sh, then run the installer again |
| On macOS, the build cannot find `cc` or `make` | Run `xcode-select --install` and wait for it to finish |
| On Windows, the installer says to run inside WSL | You ran it in Git Bash, MSYS or PowerShell. Run it inside WSL |
| `tawk --doctor` reports a missing tool | Install what it names. It tells you which feature needs it |
| The QR code does not fit | Enlarge the window to 70 by 40 characters, or link with the phone number |
| tawk starts but shows no chats | It is still syncing. Recent history arrives over the first minute or two |

For anything else, give the person the full error text and point them to the issues page: `https://github.com/loganventer/tawk/issues`.

## What to tell the person at the end

Report plainly:

- What was installed and where (`tawk --version`, and the path from `command -v tawk`).
- What `tawk --doctor` said, including any optional tool that is missing and the feature it affects.
- Anything you could not do yourself, such as a password prompt or the restart on Windows.
- What they do next: open a terminal, type `tawk`, and link their phone.
