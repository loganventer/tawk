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
| `not_found` | No visible chat or message matches. Locked and hidden chats are reported as not found. |
| `ambiguous` | A chat name matches more than one chat. `error.candidates` lists them as `{"jid","name"}`. |
| `declined` | You declined the request in tawk. |
| `timed_out` | Nobody answered in time: 5 minutes for most requests, 2 for HIGH risk ones. The request was declined. |
| `rate_limited` | Too many writes in the last minute (`writes_per_minute`). `error.retry_after` gives the seconds to wait. |
| `offline` | tawk is not linked or not connected to WhatsApp, so a write cannot be sent now. |
| `failed` | tawk tried and could not do it; `message` says why. |

## Values

A **chat** is named in arguments by its JID or by its name. A name matches case-insensitively, first exactly and then as the start of a word; when several chats match, the answer is `ambiguous`.

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
{"id":"1","op":"hello","args":{"client":"tawk-mcp","version":"0.1.0","protocol":1,"origin":"mcp"}}
```

`origin` is `mcp` for a program acting for a language model, or `cli` for a person's own command. Writes from `mcp` always ask you first; writes from `cli` ask only when `confirm_cli` is on. The origin is the client's own statement: it lets tawk treat a model more carefully than your own shell, and is no defence against other programs of your user, which can read tawk's files anyway.

Result:

```json
{
  "protocol": 1,
  "tawk": "0.8.1",
  "access": "read",
  "account": {"jid": "27830000000@s.whatsapp.net", "name": "Logan"},
  "connected": true
}
```

`access` is `read`, `send`, `manage` or `admin`.

### Reading

Reading never marks anything as read, never sends read receipts, and never changes the chat open in tawk.

| Operation | Arguments | Result |
| --- | --- | --- |
| `list_chats` | `filter` (text in the name), `unread_only` (bool), `limit` (1 to 500, default 50) | `{"chats":[chat…]}`, newest first with pinned chats on top |
| `read_messages` | `chat` (required), `before` (Unix seconds, for older pages), `limit` (1 to 200, default 30) | `{"chat":chat,"messages":[message…],"next_before":ts}`, oldest first; `next_before` is 0 when there is nothing older stored |
| `search_messages` | `query` (required), `chat`, `limit` (1 to 100, default 20) | `{"messages":[message…]}`, newest first |
| `unread_summary` | none | `{"total":5,"mentions":1,"chats":[chat…]}` with only the chats that have unread messages |
| `chat_info` | `chat` (required) | `{"chat":chat,"about":"…","members":[{"jid","name","admin"}]}`; `members` only for groups |
| `list_statuses` | `include_archived` (bool) | `{"statuses":[{"id","author","author_name","from_me","type","text","ts","viewed"}]}` |
| `list_scheduled` | `chat` | `{"scheduled":[{"id","chat","text","due_at"}]}` |

### Writing

Writes need `access = send` (or `manage` or `admin`) in `[automation]`. Each is checked against the chat list, counted against `writes_per_minute`, written to the automation log (the Log in tawk's Agentic tab) and, where the origin requires it, shown to you in the Agentic tab to approve, edit or decline. While a write waits, tawk sends `{"evt":"approval","id":"<request id>","state":"waiting"}` once, so a client can tell its user where to look.

| Operation | Arguments | Result |
| --- | --- | --- |
| `send_message` | `chat` (required), `text` (required, up to 65536 bytes), `reply_to` (a message id in that chat) | `{"id":"3EB0…"}`, the new message's id; it is queued and goes out as any message you send |
| `react` | `message_id` (required), `emoji` (required; an empty string removes your reaction) | `{}` |
| `schedule_message` | `chat` (required), `when` (as `/later` takes it: `18:00`, `+30m`, `tomorrow 9:00`, `fri 17:30`, optionally followed by an adjustment in seconds such as `+37s` or `-12s`, which never moves it into the past), `text` (required) | `{"id":"…","due_at":ts}` |
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
| `download_media` | `message_id` | `{}`; the file is fetched in the background |

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
- `{"evt":"chat","chat":chat}` when a subscribed chat's unread count changes.
- `{"evt":"bye"}` just before tawk quits.

Chats that are locked, hidden or outside `chats` never produce notifications.

## Example

```text
→ {"id":"1","op":"hello","args":{"client":"tawk","version":"0.8.1","protocol":1,"origin":"cli"}}
← {"id":"1","ok":true,"result":{"protocol":1,"tawk":"0.8.1","access":"send","account":{"jid":"27830000000@s.whatsapp.net","name":"Logan"},"connected":true}}
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

The protocol version is 1. Fields may be added to results and notifications without a new version, so clients must ignore fields they do not know. Removing or changing a field, or changing an operation's meaning, raises the version.
