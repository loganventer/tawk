# Release notes

What changed in each version of tawk, newest first. `tawk --update` shows every section above the version you have, before it asks whether to update.

Each version has a section headed `## <version> (<date>)`, with one line for each change you would notice.

## 0.14.0 (2026-10-10)

- TL;DR mode, for one chat at a time. Switch **TL;DR** on on a chat's contact card and its long messages show as a short summary, so a paragraph need not be read to know what it says. Off for every chat until you switch it on.
- Each summarised message folds open and shut by itself: Enter or a click on it shows the original under `▾ TL;DR · original`, and again brings the summary back under `▸ TL;DR`. The message menu has the same as "TL;DR: original / summary". Copy, reply, forward and search always use the original.
- tawk writes no summary itself: a connected agent's model does. "TL;DR from (characters)" under Settings, Chats sets how long a message must be (300 by default). A message with no summary yet shows as it always did.
- You choose which agent writes them: in the Agents list, **d** makes the selected agent your default agent (marked ★ DEFAULT), and d again takes that back. The choice is remembered when that agent connects again. With none chosen, the only agent connected is used.
- With several agents connected and none chosen, tawk asks you on WhatsApp: it sends a numbered list to your own "message yourself" chat, and the number you answer with there chooses the agent. This is the one message tawk ever sends by itself, and it goes to nobody but you.
- A summary goes when its message goes, and when the message is edited, since it would no longer fit. Switching TL;DR off for a chat keeps its summaries.
- A TL;DR chat's long messages of the last 30 days are summarised by themselves, newest first, when you switch it on ("TL;DR back (days)" under Settings, Chats; 0 turns that off).
- Older messages beyond that are filled in as you look at them, never a whole chat at once: a long message in a TL;DR chat that has no summary is asked for when it comes onto the screen, and so is a voice note without a transcript, while voice notes are transcribed as they arrive. Summaries and transcripts are kept in the database, so they are there after a restart.
- For agents: a `transcript_wanted` event for such a voice note, a `summary_wanted` event to the agent that writes them, `set_summary` and `get_summary` operations, a `tldr_off` error, `"tldr":true` on `chat_info`, and `summaries` in `hello`'s features. tawk-mcp 0.10.0 passes the request on as a channel event and offers `set_summary` as a tool.

## 0.13.0 (2026-10-10)

- A voice note that has been transcribed shows its words in its own bubble, under the play line, in grey italics. tawk still transcribes nothing itself: the words come from an agent's transcriber (tawk-mcp), and tawk now keeps them in its database, so they are still there after a restart and are covered by `tawk --encrypt` and backups.
- "Voice note transcripts" under Settings, Chats turns the display on and off for every chat, and "Transcript lines" sets how many lines show before the rest is cut. The message menu's "Show transcript" opens the whole text, in every language it was written in, whether or not transcripts are shown.
- Each chat has its own choice on its contact card: **Show transcripts** steps through as the setting says, always and never. Alt+T (Option+T on a Mac) and `/transcripts` step the same row for the open chat.
- The contact card also has **Transcribe voice notes**. Switched off, nothing in that chat is transcribed from then on, whether as it arrives or because an agent asked. It looks forwards only: the transcripts the chat already has are kept and still shown.
- A transcript goes when its message goes: deleted here, deleted for everyone, or cleared with its chat.
- For agents: new `set_transcript` and `get_transcript` operations, a `transcripts_off` error for a chat that is switched off, `"transcribe":false` on `chat_info`, on `download_media` and on the `message` and `media_ready` events of such a chat, and a `features` list in the answer to `hello` naming what this tawk can do, starting with `transcripts`.

## 0.12.0 (2026-10-07)

- For agents: a new `presence` operation answers whether the person in a chat is online and when they were last seen. It is off until you switch on "Look up online status" under Settings, Automation. It covers one person at a time, only in the chats agents may use, never a group or a locked chat, and it learns nothing while tawk shows you as offline. tawk-mcp 0.8.0 offers it as the `get_online_status` tool.

## 0.11.0 (2026-10-07)

- An open one-to-one chat says "online" under the person's name, or when they were last seen, for people who share that with you. It appears a moment after you open the chat and follows them as they come and go. "Show online status" under Settings, Chats turns it off.
- It needs "Appear online" on, and it shows only what the other person shares. Groups and soft-locked chats show none.
- For agents: a new `presence` event says when the person in a chat you opened comes online or leaves. It is off until you switch on "Push online status" under Settings, Automation, Agent events, it covers only the chats agents may use, and an agent cannot ask about anyone else. tawk-mcp 0.7.0 passes it on as a channel event with `--channel-presence on`.
- With the Baileys backend, someone coming online no longer clears their typing notice.

## 0.10.0 (2026-10-06)

- An agent, and `tawk send`, can write to someone you have no chat with yet. Name them by phone number with its country code (`+27821234567`), by JID, or by the name of a contact; the message starts the chat. A name two contacts share is refused and both are offered.
- You are asked about such a message as "start a new chat with this message", and it is always yours to answer. An allowance for the session does not cover it, and an agent with an admin token cannot answer it for you.
- The chats agents may use still decide who can be reached, and someone whose chat is locked is not found by number either.
- Voice note transcription starts on `large-v3-turbo` in place of `tiny`, which could not follow Afrikaans or mixed languages.

## 0.9.1 (2026-10-06)

- The Agents list gives each connected agent two lines: who it is on the first, and under it what the agent says it is working on.
- tawk-mcp 0.4.0 transcribes voice notes inside itself, with the model you choose under Settings, Automation, Voice note transcription. Nothing else needs installing, and a model that is not on this computer yet is downloaded the first time it is needed.

## 0.9.0 (2026-10-06)

- Updating shows what is new. `tawk --update` now lists every change since the version you have, from these notes, before it asks.
- Options can be written with two dashes, one or none: `tawk --update`, `tawk -update` and `tawk update` are the same. After `tawk send`, `tail` and `unread`, an option still needs a dash (`-json` or `--json`), since a bare word there is a chat name or part of the message.
- The Agentic tab always opens on the Queue, the requests that wait for you. With agent access off, it says so and points to Permissions.
- The Agents list tells sessions apart. Each connected agent shows what it says tells it apart (the folder it runs in and how it was started) and, in its own words, what it is working on.
- Settings, Automation has a new submenu, Voice note transcription: the model, chosen from a list with `tiny` as the default, the languages, and a switch for transcribing every voice note as it arrives. tawk-mcp does the transcribing and reads these choices; an agent cannot change them. The settings show only while an agent is connected, and otherwise the submenu says "No agent connected".
- For agents: `download_media` names the file when it is already on this computer, and a `media_ready` notification follows a download that ends later. tawk-mcp uses these to show pictures and transcribe voice notes.
- For agents: `hello` takes a `label`, and a new `describe` operation takes a line about what the session is doing.
