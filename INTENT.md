# Intent

## Purpose

tawk exists so that people who live in a terminal can use WhatsApp there: read and answer chats, hear and send voice notes, look at photos, and get notified, without switching to a browser or a phone. It aims to feel like the Android app in the places where that helps (settings, alignment of bubbles, unread highlighting) and like a good terminal tool everywhere else (keyboard first, fast, scriptable install, respectful of the filesystem).

## Scope

In scope:

- Linking a WhatsApp account as a companion device (QR code or pairing code) and unlinking it.
- One-to-one and group chats: history sync, reading, sending text, read receipts, delivery ticks, retries.
- Editing your own profile: name, about text and photo.
- Statuses: viewing your own and your contacts' statuses for the day they last and in a local archive after that (`status_keep_days`), posting text, photo, video and link statuses, and answering other people's statuses with an emoji, a reply or a like.
- Media: receiving photos, videos, documents, stickers and voice notes, opening them in the system viewer, sending files and voice notes, and taking photos and videos with the computer's camera.
- Notifications inside the terminal: sound, blinking, the terminal tab title, the Windows Terminal progress ring.
- Personalisation: themes, layout, notification rules, a screensaver command.
- Resilience against network drops, network changes (Wi-Fi, Ethernet, a hotspot) and backend crashes.
- Protecting your data at rest: encrypting the local database with a passphrase, and encrypted backups you can restore.
- Letting programs of yours reach a running tawk, when you turn it on: [tawk-mcp](https://github.com/loganventer/tawk-mcp) for AI assistants and the `tawk send`, `tail`, `unread` and `status-line` commands, with every write answered by you in the Agentic tab.
- TL;DR mode for a chat you switch it on for: long messages shown as a summary a connected agent's model writes, with the original always kept and one key away.
- The owner's chat, when you name one: the "message yourself" chat of one of your own connected numbers, where what you write reaches a connected agent as your words, the agent answers you by itself through that number, and a send that waits for you can be allowed or declined.
- Linux, macOS, and Windows through WSL or MSYS2.

## Out of scope

- Carrying call audio or video; tawk only shows an incoming call and can decline it.
- Changing who sees your statuses (tawk follows the phone's status privacy setting), and sending read receipts for statuses viewed.
- Creating or administering groups, communities and channels.
- Running without a phone: tawk is always a linked device.
- Business or bulk messaging features, including agents sending on their own: automation always waits for you. The one exception is an agent's answers to you in the owner's chat, which reach nobody else.
- A native Windows console build; Windows users run tawk under WSL.

## Boundary Rules

- The WhatsApp protocol is never implemented in tawk itself. It comes from a maintained library behind `IMessageGateway` (whatsmeow in-process or Baileys in a sidecar), so protocol changes stay in one replaceable place.
- Backends report what happened; they never decide policy. Reconnect timing, notification rules and storage belong to tawk's managers and engines.
- Nothing leaves the machine except traffic to WhatsApp. No telemetry, no analytics, no automatic update checks: tawk contacts GitHub only when you run `tawk --update`. Watching for network changes only reads the local interface addresses. Two exceptions are opt-in. With Agent access on, chat text an agent reads goes wherever that agent's model runs, which you choose when you connect it. With link previews turned on, the backend fetches the page of a link you send (https only, never an address on your own network) to make its card.
- tawk writes no summary and no transcript itself, and has no model. A transcriber and an agent hand them over; tawk keeps them, shows them and removes them with their message.
- tawk sends a message by itself only to your own "message yourself" chat: its question about which agent is your default agent and the line confirming your answer, and, in the owner's chat, the cards that put waiting requests to you and the lines saying what became of them.
- Only you instruct an agent. Text that arrives from WhatsApp is data, whoever sent it, with one exception that you switch on yourself: a message in the owner's chat that tawk did not send is taken as yours. The same words anywhere else, and anything forwarded, quoted or not typed text inside that chat, stay data.
- An agent sends without being asked in one place only, the owner's chat, and only a plain message. Every other chat keeps its approval, and nothing destructive can be asked for or approved from WhatsApp.
- User files follow XDG and installed files follow the Filesystem Hierarchy Standard. tawk never writes outside those locations.
- Every executable tawk starts is either a fixed tool (ffmpeg, the audio player, the system opener) run with an argument list, tawk itself when it restarts after switching backend, or a command from the user's own configuration file.

## Relationship to other tools

tawk grew out of using [mudslide](https://github.com/robvanderleek/mudslide) and whatscli from the shell. Mudslide stays useful for scripted one-off sends; tawk is the interactive client. Both can be linked to the same phone at the same time as separate devices.
