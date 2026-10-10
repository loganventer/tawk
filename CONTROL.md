# Control Protocol

A running tawk can be reached by other programs on the same computer through its control socket: the `tawk send`, `tawk tail`, `tawk unread` and `tawk status-line` commands, and [tawk-mcp](https://github.com/loganventer/tawk-mcp), which lets MCP clients such as Claude Code read your chats and, when you allow it, send messages that you confirm in tawk.

The socket is off until you turn on **Settings → Automation → Agent access (MCP)** (`control_socket = on` in `[automation]`). What agents ask for is answered in tawk's **Agentic** tab (F3). Everything a client may see or do is decided inside tawk by the settings described in [CONFIGURATION.md](CONFIGURATION.md#automation); a client cannot widen them.

## Transport

- A Unix domain socket at `$XDG_RUNTIME_DIR/tawk/control.sock`, or `~/.local/state/tawk/control.sock` where `XDG_RUNTIME_DIR` is not set. The folder is 0700 and the socket 0600.
- tawk accepts a connection only from a process of the same user (`SO_PEERCRED` on Linux, `getpeereid` on macOS).
- Each message is one JSON object on one line, in UTF-8, ending with `\n`. A line longer than 1 MiB closes the connection.
- A client may send several requests without waiting; answers carry the request's `id` and may arrive in any order, because a write waits for your approval while reads are answered at once.
- tawk answers within one frame (about 100 ms) except for writes waiting for approval.

## Messages

A request:

```json
{"id":"7","op":"read_messages","args":{"chat":"Mom","limit":20}}
```

`id` is any string up to 63 bytes chosen by the client. `args` may be left out when an operation takes none.

A successful answer:

```json
{"id":"7","ok":true,"result":{"messages":[],"next_before":0}}
```

A failed answer:

```json
{"id":"7","ok":false,"error":{"code":"not_found","message":"No chat matches \"Mum\""}}
```

A notification, sent without a request:

```json
{"evt":"message","chat":{"jid":"27820000000@s.whatsapp.net","name":"Mom"},"message":{}}
```

### Error codes

| Code | Meaning |
| --- | --- |
| `bad_request` | The line is not JSON, the operation is unknown, or an argument is missing or of the wrong type. |
| `hello_first` | Any operation before `hello`. |
| `protocol` | The client asked for a protocol version tawk does not speak. |
| `not_allowed` | The access setting does not allow it (`access` in `[automation]` is `read`, `send`, `manage` or `admin`), the setting may not be changed from outside, or the chat is outside `chats`. |
| `bad_token` | `confirm` was given a token that is unknown, used, expired or from another connection, or `approve` was given the wrong admin token. |
| `draft_exists` | `draft_message` found a draft already waiting in that chat. |
| `unsupported` | The backend in use cannot do it (posting a status needs whatsmeow). |
| `tldr_off` | `set_summary` named a message in a chat that is not in TL;DR mode: you did not switch it on on its contact card, or the chat is soft-locked. |
| `transcripts_off` | `set_transcript` named a voice note in a chat whose voice notes are not transcribed: you switched that off on its contact card, or the chat is soft-locked. |
| `not_found` | No visible chat or message matches. Locked and hidden chats are reported as not found. |
| `ambiguous` | A chat name matches more than one chat. `error.candidates` lists them as `{"jid","name"}`. |
| `declined` | You declined the request in tawk. |
| `timed_out` | Nobody answered in time: 5 minutes for most requests, 2 for HIGH risk ones. The request was declined. |
| `rate_limited` | Too many writes in the last minute (`writes_per_minute`). `error.retry_after` gives the seconds to wait. |
| `offline` | tawk is not linked or not connected to WhatsApp, so a write cannot be sent now. |
| `failed` | tawk tried and could not do it; `message` says why. |

## Values

A **chat** is named in arguments by its JID or by its name. A name matches case-insensitively, first exactly and then as the start of a word; when several chats match, the answer is `ambiguous`.

**Someone with no chat yet** can be named in `send_message` and `schedule_message`, and nowhere else. When `chat` matches no chat, it is read as a person: a phone number with its country code (`+27 82 123 4567`, `0027821234567` or `27821234567`; spaces, dashes, brackets and dots are ignored), a personal JID (`27821234567@s.whatsapp.net`), or the name of a contact, as you saved it or as they chose it, matched like a chat's name. A number written the local way, with a leading `0`, names no country and is `not_found`. A group, a broadcast and a channel are never found this way. A name that several such contacts share is `ambiguous`, and each candidate carries `"new_chat":true`. The chats agents may use (`chats` under `[automation]`) decide here as well: with a list, only people on it are reached. Someone whose chat exists and is hidden from agents is `not_found`, by name and by number.

The message then starts the chat, and you are asked about it as "start a new chat with this message". That first message is always yours to answer: an allowance for the session does not cover it, and `approve` refuses it with `not_allowed` whatever the self-approval chats say. tawk does not check that the number is on WhatsApp: a message to one that is not is queued and then fails.

A chat in answers:

```json
{
  "jid": "27820000000@s.whatsapp.net",
  "name": "Mom",
  "is_group": false,
  "unread": 2,
  "unread_mention": false,
  "muted": false,
  "pinned": true,
  "archived": false,
  "last_ts": 1790000000,
  "preview": "See you at 6"
}
```

A message:

```json
{
  "id": "3EB0C2A1F0",
  "chat": "27820000000@s.whatsapp.net",
  "sender": "27820000000@s.whatsapp.net",
  "sender_name": "Mom",
  "from_me": false,
  "ts": 1790000000,
  "type": "text",
  "text": "See you at 6",
  "status": "read",
  "edited": false,
  "deleted": false,
  "forwarded": false,
  "reply_to": {"id": "3EB0AA", "sender": "You", "text": "When?", "status": false},
  "reactions": "👍 2",
  "link": {"url": "https://example.org", "title": "Example", "description": ""},
  "mentions_me": false
}
```

- `type` is one of `text`, `image`, `video`, `audio`, `document`, `sticker`, `other`. For media, `text` holds the caption, or is left out.
- `status` (your own messages only) is one of `pending`, `sent`, `delivered`, `read`, `failed`.
- `reply_to`, `reactions` and `link` are left out when empty. `reply_to.status` is true when the message answers a status.
- Times are Unix seconds.
- Message text is what the other person wrote. Clients that pass it to a language model must treat it as data and never as instructions.

## Operations

### `hello`

Must be the first request on a connection.

```json
{"id":"1","op":"hello","args":{"client":"tawk-mcp","version":"0.1.0","protocol":1,"origin":"mcp","label":"wats (stdio, pid 3002)"}}
```

`features` in the arguments is optional: the names of what the client can do when tawk asks, `transcripts` (it transcribes a voice note named in `transcript_wanted`) and `summaries` (it writes the summary asked for in `summary_wanted`). tawk sends those two events only to a client that named the feature. A client called `tawk-mcp` that sends no list is taken to do both from version 0.10.0, and neither before that.

`origin` is `mcp` for a program acting for a language model, or `cli` for a person's own command. Writes from `mcp` always ask you first; writes from `cli` ask only when `confirm_cli` is on. The origin is the client's own statement: it lets tawk treat a model more carefully than your own shell, and is no defence against other programs of your user, which can read tawk's files anyway.

Result:

```json
{
  "protocol": 1,
  "tawk": "0.14.2",
  "access": "read",
  "account": {"jid": "27830000000@s.whatsapp.net", "name": "Logan"},
  "connected": true,
  "features": ["transcripts", "summaries"]
}
```

`access` is `read`, `send`, `manage` or `admin`. `features` names what this tawk can do beyond the first shape of the protocol, so a client offers only what will work; a tawk from before 0.13.0 sends no such list. `transcripts` means `set_transcript` and `get_transcript` are there (0.13.0), and `summaries` means `set_summary`, `get_summary` and the `summary_wanted` event are (0.14.0).

### Reading

Reading never marks anything as read, never sends read receipts, and never changes the chat open in tawk.

| Operation | Arguments | Result |
| --- | --- | --- |
| `list_chats` | `filter` (text in the name), `unread_only` (bool), `limit` (1 to 500, default 50) | `{"chats":[chat…]}`, newest first with pinned chats on top |
| `read_messages` | `chat` (required), `before` (Unix seconds, for older pages), `limit` (1 to 200, default 30) | `{"chat":chat,"messages":[message…],"next_before":ts}`, oldest first; `next_before` is 0 when there is nothing older stored |
| `search_messages` | `query` (required), `chat`, `limit` (1 to 100, default 20) | `{"messages":[message…]}`, newest first |
| `unread_summary` | none | `{"total":5,"mentions":1,"chats":[chat…]}` with only the chats that have unread messages |
| `chat_info` | `chat` (required) | `{"chat":chat,"about":"…","members":[{"jid","name","admin"}]}`; `members` only for groups. `"transcribe":false` is added when the chat's voice notes are not to be transcribed, and `"tldr":true` when the chat is in TL;DR mode |
| `get_summary` | `message_id` (required) | `{"summary":{"text","model","at"}}`, or `{}` when the message has none |
| `get_transcript` | `message_id` (required) | `{"transcripts":[{"language","text","model","at"}]}`, newest first: what tawk keeps for a voice note, one for each language. Empty when it has none |
| `presence` | `chat` (required), a one-to-one chat | `{"chat":chat,"state":"online"\|"offline"\|"unknown","last_seen":ts,"watching":true}`. It answers with what tawk knows now and asks WhatsApp to keep telling it about that person, as opening the chat does, so the answer to a first call is usually `unknown` and a call a second later has it. `last_seen` is present only when they share it. `watching` is false while tawk is not shown as online itself, when nothing can be learnt. Origin `mcp` needs `presence_lookup` on, or it is `not_allowed`; a group is `bad_request` |
| `list_statuses` | `include_archived` (bool) | `{"statuses":[{"id","author","author_name","from_me","type","text","ts","viewed"}]}` |
| `list_scheduled` | `chat` | `{"scheduled":[{"id","chat","text","due_at"}]}` |

### Transcripts

tawk transcribes nothing. A transcriber hands the words of a voice note over, tawk keeps them beside the message and shows them in the conversation, and they are removed with their message.

| Operation | Arguments | Result |
| --- | --- | --- |
| `set_transcript` | `message_id` (required), `text` (required, at most 16 KB), `language` (a short code such as `af`; leave out or `auto` when not known), `model` | `{}`. Replaces the transcript the message already has in that language |

`set_transcript` changes only this computer and sends nothing to WhatsApp, so it is not asked about, needs no more than `read`, and does not count against `writes_per_minute`; each one is written to the automation log. It is answered `bad_request` for anything that is not a voice note or other audio, `not_found` for a message in a chat the client may not see (a locked or soft-locked one included), and `transcripts_off` for a chat whose voice notes are not transcribed. Control characters in the text are replaced, and the text is shown as plain text, never formatted. A client cannot change a chat's transcript choices: they are set on its contact card.

### TL;DR summaries

In a chat you put in TL;DR mode, tawk shows a long message as a short summary. tawk writes none: it asks one agent for each, and keeps what the agent hands back.

| Operation | Arguments | Result |
| --- | --- | --- |
| `set_summary` | `message_id` (required), `text` (required, at most 2000 bytes), `model` | `{}`. Replaces the summary the message already has |

`set_summary` changes only this computer and sends nothing to WhatsApp, so it is not asked about, needs no more than `read`, and does not count against `writes_per_minute`; each one is written to the automation log. It is answered `bad_request` for anything that is not a text message, `not_found` for a message in a chat the client may not see, and `tldr_off` for a chat that is not in TL;DR mode. The text is made one plain paragraph, with control characters and line breaks replaced. A client cannot switch a chat's TL;DR mode: that is set on its contact card.

tawk asks with an event, sent to one client only whether or not it subscribed:

- `{"evt":"summary_wanted","chat":{"jid","name"},"message":message,"max_chars":400}` for a text message from someone else (every one while `tldr_min_chars` is 0, else those at least that many characters long), in a chat in TL;DR mode that the client may see. It is sent when the message arrives, for the chat's long messages of the last `tldr_back_days` days when its TL;DR is switched on or it is first shown, and for an older one when you look at it in tawk; once for each message while tawk runs, four at once and then one every second and a half.

Which client that is: the one you made your default agent in the Agents list while it is connected; else the only client connected with origin `mcp` that named `summaries` and is not paused; else, with several such and none chosen, nobody until you choose. A client that did not name the feature is never asked and does not count. tawk then asks you in the "message yourself" chat of the account the chat is in, and the number you answer with chooses. The choice is kept in `default_agent` under `[automation]`, as the client's `label` with any ", pid N" taken out, so a client that wants to be recognised again keeps its label the same.

### Writing

Writes need `access = send` (or `manage` or `admin`) in `[automation]`. Each is checked against the chat list, counted against `writes_per_minute`, written to the automation log (the Log in tawk's Agentic tab) and, where the origin requires it, shown to you in the Agentic tab to approve, edit or decline. While a write waits, tawk sends `{"evt":"approval","id":"<request id>","state":"waiting"}` once, so a client can tell its user where to look.

| Operation | Arguments | Result |
| --- | --- | --- |
| `send_message` | `chat` (required; a chat, or someone with no chat yet), `text` (required, up to 65536 bytes), `reply_to` (a message id in that chat) | `{"id":"3EB0…"}`, the new message's id; it is queued and goes out as any message you send |
| `react` | `message_id` (required), `emoji` (required; an empty string removes your reaction) | `{}` |
| `schedule_message` | `chat` (required; a chat, or someone with no chat yet), `when` (as `/later` takes it: `18:00`, `+30m`, `tomorrow 9:00`, `fri 17:30`, optionally followed by an adjustment in seconds such as `+37s` or `-12s`, which never moves it into the past), `text` (required) | `{"id":"…","due_at":ts}` |
| `mark_read` | `chat` (required) | `{}`; sends read receipts when `send_read_receipts` is on |
| `draft_message` | `chat` (required), `text` (required) | `{"drafted":true}`; the text waits in that chat's input box in tawk for you to edit and send. Nothing is sent and nothing is asked. Fails with `draft_exists` when the chat already has a draft |

When you edit the text in the approval dialog before allowing it, the result of `send_message` and `schedule_message` also carries `"edited":true` and `"text"` with what was actually sent.

With `ai_disclaimer` on, tawk adds the line in `ai_disclaimer_text` under the text of `send_message`, `schedule_message` and `reply_status` from a client whose origin is `mcp`, after any edit of yours. The result then carries `"disclaimer":true` and `"text"` with what went out. A client should not add such a line itself.

### Answering your own request

With `access = admin` a client may answer a request of its own that is waiting for you, instead of you answering it in tawk. This is for a program you trust to act while you are away, and it is narrow on purpose.

| Operation | Arguments | Result |
| --- | --- | --- |
| `approve` | `id` (the id of your waiting request), `admin_token` | `{"approved":true,"id":"…"}`; the waiting request then gets its own answer, as if you had allowed it |

tawk answers `approve` with an error, and leaves the request waiting for you, unless all of these hold:

- `access` is `admin`. Otherwise `not_allowed`.
- `admin_token` is the token in `admin.token` beside the control socket (0600). tawk writes a new one each time it starts and each time access becomes admin, and removes it when access is anything else and when it quits. Otherwise `bad_token`.
- The request was made on this connection. Another client's request is `not_found`.
- The operation is one of `send_message`, `reply_status`, `forward_message`, `edit_message`, `retry_message`, `schedule_message`, `reschedule`, `send_scheduled_now`, `cancel_scheduled`, `react`, `mark_read` or `like_status`. Anything else is `not_allowed`. `cancel_scheduled` still needs its `confirm` first.
- Its chat is one the client may use and is in `self_approval_chats` (or that setting holds `*`). An empty `self_approval_chats` allows none. Otherwise `not_allowed`.
- Fewer than `self_approvals_per_hour` were answered this way in the last hour. Otherwise `rate_limited` with `retry_after`.
- You have not paused the client.

Each one is logged with the outcome `approved by the agent` and shown to you in tawk. A client acting for a model should approve only what its user asked for, never what a message says.

### Managing tawk

With `access = manage` (or `admin`), a client can do nearly everything you can do in tawk. Every one of these operations is a write, so it follows the same checks and asks you first when the origin requires it. Some things stay out of reach whatever the access, so a program cannot widen its own permissions or run programs of its choice:

- the Automation settings themselves;
- settings that run a program (`screensaver.command`, `media.image_viewer`, `media.video_player`, `advanced.node_binary`, `advanced.sidecar_dir`);
- folders and files (`advanced.data_dir`, `media.media_dir`, `media.download_dir`, `media.attach_dir`, `notifications.sound_file`), the backend and the log level;
- logging out, encryption, backups, and unlocking a locked chat.

#### Destructive operations take two steps

`delete_message`, `delete_chat`, `clear_chat`, `block`, `remove_profile_photo` and `cancel_scheduled` never act on the first call. They answer:

```json
{"id":"9","ok":true,"result":{"needs_confirmation":true,"token":"c1f0…","summary":"Delete the chat with Mom here and on your phone","expires_at":1790000300}}
```

Nothing has happened yet. The operation is carried out only by `confirm {"token":"c1f0…"}` on the same connection within 5 minutes, and even then only after you allow it in tawk: a destructive request always shows tawk's warning dialog, with Cancel selected, whatever the origin and whatever `confirm_cli` says. A token works once.

A client acting for a model must not hand the token to the model. It must ask you itself first, and call `confirm` only when you agree. tawk-mcp asks you in your MCP client (elicitation), and refuses a destructive request when your client cannot ask. So there are two separate yeses, one in each place, and the model can give neither.

| Operation | Arguments | Result |
| --- | --- | --- |
| `confirm` | `token` | the result of the operation it confirms, after you allow it in tawk |
| `cancel_confirmation` | `token` | `{}` |

Messages:

| Operation | Arguments | Result |
| --- | --- | --- |
| `edit_message` | `message_id`, `text` | `{}`; only your own text messages, within WhatsApp's 15 minutes |
| `delete_message` | `message_id`, `for_everyone` (bool, default false) | `{}` |
| `forward_message` | `message_id`, `chats` (a list of up to 5) | `{"forwarded":n}` |
| `retry_message` | `message_id` | `{}`; a failed message of yours |
| `download_media` | `message_id` | `{"path":"…","type":"image","chat":{"jid","name"}}` when the file is already on this computer; `{}` when it is being fetched in the background, and a `media_ready` notification follows for a client that subscribed |

Chats:

| Operation | Arguments | Result |
| --- | --- | --- |
| `set_chat` | `chat`, and any of `muted` (`false`, `true` for always, or seconds), `pinned`, `archived` (bools), `locked` (only `true`: hides the chat behind the soft lock, which also hides it from clients) | `{"chat":chat}` |
| `set_chat_theme` | `chat`, `theme` (a theme id, or `""` for the app theme) | `{}` |
| `clear_chat` | `chat` | `{}`; removes its messages from this computer |
| `delete_chat` | `chat` | `{}`; deletes the chat here and on your phone |
| `export_chat` | `chat`, `with_media` (bool) | `{"path":"…"}`, a folder in your downloads folder |
| `block` / `unblock` | `chat` (a person) | `{}` |

Messages to send later:

| Operation | Arguments | Result |
| --- | --- | --- |
| `cancel_scheduled` | `id` | `{}` |
| `reschedule` | `id`, `when` (as for `schedule_message`) | `{"due_at":ts}` |
| `send_scheduled_now` | `id` | `{}` |

Statuses:

| Operation | Arguments | Result |
| --- | --- | --- |
| `status_viewers` | `status_id` (one of yours) | `{"viewers":[{"jid","name","ts","liked"}]}`; a read, needs only `read` |
| `post_status` | `kind` (`text`, `photo`, `video`, `link`), `text` (the words or caption), `file` (a photo or video on this computer), `background` (a colour name from `list_backgrounds`, text and link only) | `{"state":"posting"}`; tawk shows when it is posted. Needs the whatsmeow backend |
| `list_backgrounds` | none | `{"backgrounds":["…"]}`; a read |
| `reply_status` | `status_id`, `text` | `{"id":"…"}`; goes to your chat with its author, quoting it |
| `like_status` | `status_id` | `{"how":"like"}`, or `{"how":"reply"}` where the backend sends a ❤️ reply instead |

Your profile and the app:

| Operation | Arguments | Result |
| --- | --- | --- |
| `get_profile` | none | `{"jid","name"}`; a read |
| `set_profile` | `name` and/or `about` | `{}`; applied in the background |
| `set_profile_photo` | `file` (a picture on this computer) | `{}` |
| `remove_profile_photo` | none | `{}` |
| `get_settings` | none | `{"settings":[{"section","key","label","help","kind","value","choices","min","max","changeable"}]}`; a read |
| `set_setting` | `section`, `key`, `value` (as text: `on`/`off`, a number, a choice) | `{"value":"…"}`, the value stored after its bounds were applied |
| `list_themes` | none | `{"themes":[{"id","name"}]}`; a read |
| `app_status` | none | `{"tawk","backend","connected","state","detail","ringing"}`; a read |
| `describe` | `text` | `{}`; a line in the agent's own words about what it is working on (tawk-mcp keeps it to ten words), shown beside it in the Agents list so its sessions can be told apart. `""` takes it away. A read |
| `reconnect` | none | `{}` |
| `decline_call` | none | `{}`; declines the call ringing now |

### Live updates

| Operation | Arguments | Result |
| --- | --- | --- |
| `subscribe` | `chats`: a list of chats, or `"all"` | `{}`; replaces the connection's earlier subscription |
| `unsubscribe` | none | `{}` |

After `subscribe`, tawk sends:

- `{"evt":"message","chat":{"jid","name"},"message":message}` for every new message, sent or received, in a subscribed chat. For a client whose origin is `mcp`, `push_received` and `push_sent` in `[automation]` decide whether received and sent messages are sent at all; origin `cli` always gets both.
- `{"evt":"read","chat":{"jid","name"},"message_id":"…","reader":{"jid","name"},"at":ts}` when someone reads a message you sent in a subscribed chat. Only for origin `mcp`, and only with `push_read` on.
- `{"evt":"reaction","chat":{…},"message_id":"…","who":{"jid","name"},"emoji":"👍","at":ts}` when someone reacts to a message you sent; `emoji` is `""` when the reaction is taken back. Origin `mcp` with `push_reactions` on.
- `{"evt":"edit","chat":{…},"message_id":"…","who":{…},"message":message,"at":ts}` when someone changes a message they sent, with the message as it now reads, and `{"evt":"delete","chat":{…},"message_id":"…","who":{…},"at":ts}` when they delete one for everyone. Origin `mcp` with `push_edits` on.
- `{"evt":"scheduled_sent","chat":{…},"message_id":"<the scheduled message's id>","at":ts}` when a message you scheduled goes out. Origin `mcp` with `push_scheduled` on.
- `{"evt":"media_ready","chat":{…},"message_id":"…","path":"…","type":"audio","at":ts}` when a message's photo, voice note or file has finished downloading, whoever asked for it. `path` is the file in tawk's media folder and `type` the message's type. Origin `mcp`.
- `{"evt":"presence","chat":{…},"who":{"jid","name"},"state":"online","last_seen":ts,"at":ts}` when the person in a subscribed one-to-one chat comes online or leaves. `state` is `online` or `offline`; `last_seen` is present only when they share it; there is no `message_id`. It is sent on a change, not on every notice. tawk only knows this for a chat the user has opened since connecting, or one a client asked about with `presence`. Origin `mcp` with `push_presence` on.
- `{"evt":"transcript_wanted","chat":{"jid","name"},"message_id":"…"}` for an older voice note you looked at in tawk that has no transcript, sent to one client with origin `mcp` that named `transcripts`, whether or not it subscribed, and once for each voice note while tawk runs. Only while `transcribe_auto` is on, only for someone else's voice note, and only in a chat that is transcribed and shows transcripts. The client transcribes it as it does one that arrives.
- A `message` event for a voice note, and a `media_ready` event for audio, carry `"transcribe":false` when the chat's voice notes are not to be transcribed, so a transcriber knows before it starts.
- `{"evt":"chat","chat":chat}` when a subscribed chat's unread count changes.
- `{"evt":"bye"}` just before tawk quits.

Chats that are locked, hidden or outside `chats` never produce notifications.

## Example

```text
→ {"id":"1","op":"hello","args":{"client":"tawk","version":"0.14.2","protocol":1,"origin":"cli"}}
← {"id":"1","ok":true,"result":{"protocol":1,"tawk":"0.14.2","access":"send","account":{"jid":"27830000000@s.whatsapp.net","name":"Logan"},"connected":true}}
→ {"id":"2","op":"unread_summary"}
← {"id":"2","ok":true,"result":{"total":2,"mentions":0,"chats":[{"jid":"27820000000@s.whatsapp.net","name":"Mom","is_group":false,"unread":2,"unread_mention":false,"muted":false,"pinned":true,"archived":false,"last_ts":1790000000,"preview":"See you at 6"}]}}
→ {"id":"3","op":"send_message","args":{"chat":"Mom","text":"On my way"}}
← {"id":"3","ok":true,"result":{"id":"3EB0D41C22"}}
```

## Accounts

A tawk with accounts says so in `hello`, and serves each request from the account it names. Nothing here changes the meaning of an existing field, so the protocol version stays 1.

**`hello`** gains three fields:

| Field | Meaning |
|---|---|
| `multi_account` | `true`. A tawk from before accounts has no such field, and ignores `account` in a request: a client must not name an account to it |
| `accounts` | The accounts this client may use: `[{id, label, jid, name, connected, primary, access}]`. `access` is `read`, `send`, `manage` or `admin` |
| `default_account` | The id of the account that serves a request naming none: the primary account if agents may use it, else the lowest id they may use. `0` when no account is open to agents |

The single `account` object and the top-level `access` stay, and describe the default account. With no account open to agents the `accounts` list is empty and `account.jid` is `""`.

**Every operation** accepts `account` in its arguments: an account's id, as a number or a string, or its label in any case. A chat reference is resolved inside that account, so the same person on two accounts is two chats. What an operation may do is decided by that account's own level.

```json
{"id":"7","op":"send_message","args":{"account":"work","chat":"Mom","text":"On my way"}}
```

**A new message follows the contact.** `send_message`, `schedule_message` and `draft_message` that name no account are served by the account you send to that contact from, chosen as the chat list chooses it: the contact's own sending number, else the default account if it has the chat, else the account whose chat with them is newest. A `send_message` with `reply_to` goes from the account that holds the quoted message, as a reply does in tawk itself. Naming an `account` overrides all of this. When the contact's sending number is an account closed to agents the request answers `not_allowed`, and nothing is sent from another number. Every other operation that names no account is served by the default account. The result of a write, and its `approval` notification, carry the `account` object of the account that served it.

An account whose level is *off* is not listed and cannot be named. Naming it answers `not_found` with `No such account`, exactly as a name no account has, so a client learns nothing about it. A request naming no account while none is open answers `not_allowed`.

**`list_accounts`** is a read operation with no arguments. It answers `{"accounts":[...],"default":id}` with the same objects as `hello`, read afresh, since you can open or close an account while a client stays connected.

**Notifications** `message`, `read`, `reaction`, `edit`, `delete`, `scheduled_sent` and `chat` carry `"account":{"id":2,"label":"work"}`. A message that reaches an account closed to agents is not pushed. A subscription follows a chat by its JID in every account the client may use, and unread counts are watched in each of them. An account that is opened to agents while a client is connected starts from that moment: nothing from before is replayed.

**`ambiguous`** candidates carry the same `account` object, beside `jid` and `name`.

**A request that waits for an answer** belongs to the account it was asked of. It is carried out by that account whatever was served meanwhile, `approve` is judged by that account's rules, and it is refused with `not_allowed` if the account was closed to agents while it waited. The automation log records the account of every entry.

## Versions

The protocol version is 1. Fields may be added to results and notifications without a new version, so clients must ignore fields they do not know. Operations and events are added the same way, and `features` in the answer to `hello` names the groups a client can rely on: `transcripts` since tawk 0.13.0, `summaries` since 0.14.0. A client that meets an event it does not know ignores it. Removing or changing a field, or changing an operation's meaning, raises the version.
