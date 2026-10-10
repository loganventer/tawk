# Release notes

What changed in each version of tawk, newest first. `tawk --update` shows every section above the version you have, before it asks whether to update.

Each version has a section headed `## <version> (<date>)`, with one line for each change you would notice.

## 0.17.0 (2026-10-10)

Notifications that reach you outside tawk, and stay quiet when you want them to.

- **System notifications** (Settings, Notifications; off by default). `terminal` asks the terminal itself for a banner with the escape code it understands (iTerm2, kitty, WezTerm, Ghostty, foot, urxvt); `desktop` runs `terminal-notifier` or `osascript` on macOS and `notify-send` elsewhere; `both` does both. The banner carries the message only when "Show preview" is on, and otherwise says "New message". Inside tmux or screen the terminal kind usually does not get through; use `desktop` there.
- **Quiet hours** (`quiet_hours`, such as `22:00-07:00`), with other hours for Saturday and Sunday if you give them (`quiet_hours_weekend`). Nothing alerts you inside them except a mention, when "Mentions always notify" is on. Unread counts still go up.
- **Notify me**, a new row on every contact card: for every message, or only when mentioned. A busy group set to mentions only stays silent until someone names you. It holds for that group on all your numbers.

## 0.16.0 (2026-10-10)

Tighter control over what agents see and do, chat by chat.

- **Agents here**, a new row on every contact card, steps through four choices for that chat: as the account says, always ask me, read only, hidden from agents. A rule only tightens what the account's level allows, and holds for that person or group on all your numbers.
  - *Always ask me*: every send there is yours to answer, each time. "Allow for this session" does not cover it and an agent cannot answer its own request.
  - *Read only*: agents may read the chat and are refused any write in it, without you being asked.
  - *Hidden from agents*: the chat is not listed, cannot be read, searched or named, and nothing about it is pushed. It reads the same as a chat that does not exist.
- **Codes and card numbers are hidden from models.** A one-time code in a message that speaks of a code, PIN, password or verification reaches an agent acting for a model as `[code]`, and a card number as `[card number]`, in messages, quoted text, link cards and chat previews. Your own shell commands (`tawk tail`, `tawk unread`) see the text as it is. On by default; Settings, Automation, "Hide codes and card numbers from agents" (`mask_codes`) switches it off.

## 0.15.0 (2026-10-10)

The owner's chat: talk to your agent, and answer its requests, from WhatsApp on your phone.

- Open your own "message yourself" chat, press its contact card, and switch on **This is my chat with the agent**. It is off until you do, and an agent cannot switch it on.
- What you type there on your phone reaches your agent as your own words. Anything forwarded, quoted, or not typed text (a voice note, a file) is passed on as data, never as an instruction, and what you write in any other chat never is.
- The agent answers you there by itself, with no approval: a message to yourself reaches nobody else. It has its own limit per hour (60), and every answer is in the Agentic tab's log.
- A send to anyone else still waits for you, and after 20 seconds unanswered in tawk it is also put to you in the owner's chat as a card with the exact words. Reply to the card with `y` or `n`, or react with a thumbs up or down. Reply with other words and they replace the text, which is read back on a new card before anything goes.
- Deletes, blocks, settings and profile changes, and a first message to someone new are never put to you there and cannot be allowed from WhatsApp.
- tawk tells your messages from the agent's by remembering what it sent, since both carry your number. It does not check which of your devices wrote a message: anyone at a device linked to that number can instruct the agent within these limits.
- Needs tawk-mcp 0.11.0 or later for the agent to hear you. Approving from WhatsApp works with any client.
- New settings under Automation: `owner_chat`, `owner_replies_per_hour`, `owner_approvals`, `owner_card_wait`. New control event `owner_message`, and `owner_chat` in the `features` of hello.

## 0.14.5 (2026-10-10)

- Fixed: summaries could stop for good. An agent that let two requests go unanswered was passed over until it reconnected, and an agent that was only busy with other work, or the only one connected, was switched off that way. It is now passed over for five minutes and then tried again, and any summary it hands back clears it at once.

## 0.14.4 (2026-10-10)

- TL;DR covers both sides of a chat. With it on, your own messages are summarised as well as the other person's, new ones and those of the last 30 days.

## 0.14.3 (2026-10-10)

- Fixed: with two agents connected, voice notes could still come out as English translations, and summaries never arrived. tawk handed a voice note to the first agent that could transcribe, which could be a session still running an older tawk-mcp; it now hands it to the one running the newest. And an agent that is asked for summaries and never hands one back (a session that takes no channel events) is passed over after two unanswered requests, which go to another agent; tawk tells you when it does this.
- The transcripts written so far are dropped once more when tawk starts, since some came from the older transcriber, and are written again as their voice notes are looked at.
- English is the only voice note language switched on to begin with. Switch on the others you hear under Settings, Chats, Voice note languages, or for one chat on its contact card. A settings file from an earlier version keeps what it says (usually `auto`, which is every language) until you choose.
- A chat's list of languages opens with the ones that apply to it switched on, so you see what it uses now.
- Settings, Automation, Voice note transcription has the same list of switches for the languages, where it had a line of text to type codes into.

## 0.14.2 (2026-10-10)

- A transcript is shown whole. It is no longer cut to six lines, and the "Transcript lines" setting is gone.
- Voice note languages. A voice note is written out in the language spoken, and you can say which languages those are. Settings, Chats has "Voice note languages" with "Choose the languages…": a list of every language with a switch for each, for all chats. A chat's contact card has "Voice note languages…" with the same list for that chat alone, which comes first. Afrikaans and English are switched on to begin with (a settings file from an earlier version keeps what it says, usually `auto`, until you choose). With languages switched on, the transcriber chooses among them for each voice note; with none, it chooses among all. tawk-mcp 0.10.2 does the choosing, and writes a note that mixes a language with English in that language, where it used to produce an English translation.
- Transcripts written before this version are dropped when tawk starts, since their language could be wrong, and are written again as their voice notes are looked at. A voice note that is written out again replaces the transcript it had.
- Summaries are asked for in the language the message is written in.
- Every message of a TL;DR chat is summarised without changing a setting: the length setting has a new name in the settings file (`tldr_from_chars`), so the 300 that 0.14.0 wrote there no longer applies.
- In the input, the arrows held with Ctrl, Alt or Shift step a word at a time, and up and down held with them go to the start and the end of what you typed. Option with the arrows does the same on a Mac.
- A click anywhere in the conversation leaves the cursor in the input, ready to type. A click on a message still opens, plays or unfolds it.
- Fixed: tawk's question about which agent writes summaries could fail silently just after start, while the account was still connecting, and was then not tried again for ten minutes. It is now tried again after fifteen seconds.

## 0.14.1 (2026-10-10)

- In a chat with TL;DR on, every message from someone else is now summarised, not only long ones. "TL;DR from (characters)" under Settings, Chats is 0 by default, which means every message; raise it to summarise only longer ones. If you ran 0.14.0, your settings file still says 300: set it to 0 there.
- A summary shows in place of its message only when it is shorter than the message. One that is no shorter saves nothing, so the message shows as it is.
- Fixed: nothing was transcribed or summarised when an agent running an older tawk-mcp was connected too. tawk handed the request to the first agent connected, and an older one ignored it. tawk now asks only an agent that says it can do the work (tawk-mcp 0.10.1 says so when it connects, and 0.10.0 is recognised by its version), and an older one no longer counts when tawk decides whether to ask you which agent to use.
- tawk's question about which agent writes summaries now goes to the "message yourself" chat of the number the chat is on, which is the one you are reading, where before it went to the primary account's.

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
