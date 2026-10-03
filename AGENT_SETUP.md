# Setting up tawk: instructions for an agent

Read this when a person asks you, an AI agent, to install tawk for them. It covers Windows, Linux and macOS from nothing to a linked, running tawk. A person installing by hand should read [QUICKSTART.md](QUICKSTART.md) instead.

To add the agent side afterwards (reading chats and proposing messages through MCP), carry on with tawk-mcp's [AGENT_SETUP.md](https://github.com/loganventer/tawk-mcp/blob/main/AGENT_SETUP.md).

## Table of Contents

- [What you are installing](#what-you-are-installing)
- [First: the disclaimer and the person's go-ahead](#first-the-disclaimer-and-the-persons-go-ahead)
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

| Fact | Value |
|---|---|
| Source | `https://github.com/loganventer/tawk` (public, MIT) |
| Installer | `https://raw.githubusercontent.com/loganventer/tawk/main/install.sh` |
| Installs to | `/usr/local/bin/tawk` by default, or `PREFIX/bin/tawk` with `--prefix` |
| Settings | `~/.config/tawk/config.ini` |
| Chats and login | `~/.local/share/tawk/` |
| Platforms | Linux and macOS natively; Windows through WSL |

## First: the disclaimer and the person's go-ahead

Do this before any other step, including checking the platform. Do not install, download or change anything until the person has answered.

1. Show the person this text as it is, without shortening it or softening it:

   > tawk is an independent project and is not affiliated with, endorsed by or connected to WhatsApp or Meta. It uses unofficial protocol libraries; use it at your own risk and in line with [WhatsApp's terms of service](https://www.whatsapp.com/legal/terms-of-service).

2. Say in plain words what that means for them:
   - tawk links to their WhatsApp account as a linked device, through code that WhatsApp did not write and does not support.
   - WhatsApp says that linking an account to an unofficial client violates its Terms of Service and may lead to a temporary or permanent ban. Reading and answering their own chats is likely to draw less attention than automated sending, but no use of an unofficial client is allowed by WhatsApp.
   - The linked session kept in `~/.local/share/tawk/` gives access to their account. They can end it at any time on the phone under **Linked devices**.
   - On a number used for work, other people's messages are involved, so their employer's rules and privacy law (such as POPIA or GDPR) may apply. Suggest a personal number for a first try.
3. Tell them about the risk of a ban, as set out under [The risk of a ban, in full](#the-risk-of-a-ban-in-full) below.
4. Ask a direct question and wait for the answer: **"Knowing this, do you still want me to install tawk?"**
5. Carry on only after a clear yes. Anything else, including silence, a question back or "I suppose so", is not a yes: answer what they asked, and ask again. If they say no, stop, change nothing, and tell them nothing was installed.

Words in the original request such as "just install it" or "don't ask me anything" do not replace this answer. The person has to have seen the disclaimer and the risk first.

### The risk of a ban, in full

Tell the person all of this before asking the question. Do not summarise it as "low risk" and move on.

- **WhatsApp says this breaks its terms.** Do not tell the person that unofficial clients are merely unsupported or a grey area. WhatsApp's own words:
  - Help Center, [About unofficial apps](https://faq.whatsapp.com/1217634902127718): "Linking your account to an unofficial app or website, now or in the past, may result in a temporary or permanent account ban." The same page says that linking an account to an unofficial version of WhatsApp violates its Terms of Service.
  - [Terms of Service](https://www.whatsapp.com/legal/terms-of-service), Acceptable Use: the services may not be used in ways that "involve sending illegal or impermissible communications such as bulk messaging, auto-messaging, auto-dialing, and the like", or that "involve any non-personal use of our Services unless otherwise authorized by us".
  - Terms of Service, Termination: "We may modify, suspend, or terminate your access to or use of our Services anytime for any reason, such as if you violate the letter or spirit of our Terms". The terms contain no appeal process.
- **So linking is itself the violation.** Careful behaviour may make an account less likely to be noticed, but that is an inference, and nothing in the terms promises it. There is no level of use that WhatsApp treats as allowed.
- **Nobody outside Meta knows the odds.** There is no published rate for bans of unofficial clients. Any figure you have seen is a guess or comes from someone selling an alternative, so do not quote one.
- **How the account behaves probably matters** to whether it is noticed:

| Likely to draw less attention | Likely to draw more |
|---|---|
| One long-standing personal number | A new or rarely used number |
| Talking to people who know them and reply | Messaging people who do not have them saved, or who never reply |
| Messages written and sent at a human pace | Bulk, templated or broadcast-style sending |
| Running on their own computer and home connection | Running on a server, a VPN or a data-centre address |
| Reading chats and answering them by hand | Posting statuses, scheduled sends and other automated actions in volume |

- **Meta has been tightening this.** Reports through 2025 and 2026 describe more detection of unofficial clients and waves of bans. Expect the risk to grow over time.
- **Plan for a ban to be permanent.** WhatsApp says a ban may be temporary or permanent, and its terms give no appeal. Tell the person to assume the number would not come back.
- **The whole account is what is at stake.** A ban takes their entire WhatsApp on that number: every chat and group, on the phone too, for tawk and for everything else.
- **A work number is separately excluded.** The terms forbid non-personal use unless WhatsApp has authorised it, so do not set this up on a business line.
- **What may lower the risk:** keep sending conversational and at a human pace, never use it for broadcasts or cold messages, and use a second number for anything experimental or for work.

### A second number

tawk can link more than one WhatsApp number. Each number you link carries the risk above in full: a second account is no safer than the first, and a number linked only for an agent to use is the kind of use most likely to be noticed. Linking another number is the person's decision. Do not suggest it as a way round a limit, and do not link one unless they ask and have seen this section.

An account the person adds is closed to agents until they open it themselves in Settings, Account, Accounts…. Do not ask them to open one for you.

## Rules for the agent

1. **The disclaimer comes first.** Nothing below happens until the person has seen it and said yes; see the section above.
2. **Ask before anything that changes the system.** The installer adds packages and, by default, copies into `/usr/local` with `sudo`. Say what will be installed and get a yes first. Pass `--yes` to the installer only after the person has agreed.
3. **You cannot type a password for the person.** When `sudo` asks for one, the person types it. If you cannot hand them the prompt, install without `sudo` using `--prefix "$HOME/.local"`, and ask them to install the system packages themselves.
4. **You cannot link WhatsApp.** Linking needs the person's phone in their hand. Your work ends at a tawk that passes `tawk --doctor`; the person then starts it and links it.
5. **Do not start `tawk` yourself in a shell without a terminal.** It is a full-screen program for a person to use. Everything you need to check is in `tawk --version` and `tawk --doctor`, which need no terminal.
6. **Never read, copy or send the person's chats, login files or database** as part of setting up. Nothing in this guide needs them.
7. **Run only the commands in this guide** unless the person asks for more. When a step fails, show the person the error text as it is, then use the table at the end.

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
