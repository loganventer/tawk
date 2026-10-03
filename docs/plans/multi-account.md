# Plan: several WhatsApp accounts in one tawk

Status: plan, not started. Branch: `feature/multi-account` in `tawk` and in `tawk-mcp`, both cut from `main`.

## Table of Contents

- [Why](#why)
- [What was decided](#what-was-decided)
- [What it looks like when done](#what-it-looks-like-when-done)
- [The shape of the change](#the-shape-of-the-change)
- [Work, in order](#work-in-order)
  - [1. The account and the database](#1-the-account-and-the-database)
  - [2. Several sessions in the backend](#2-several-sessions-in-the-backend)
  - [3. One runtime per account](#3-one-runtime-per-account)
  - [4. The terminal client](#4-the-terminal-client)
  - [5. Merged chats](#5-merged-chats)
  - [6. The control socket and the shell commands](#6-the-control-socket-and-the-shell-commands)
  - [7. tawk-mcp](#7-tawk-mcp)
  - [8. Encryption, backup, doctor and logs](#8-encryption-backup-doctor-and-logs)
  - [9. Documentation](#9-documentation)
- [How the rules are kept](#how-the-rules-are-kept)
- [Risks](#risks)
- [How it is checked](#how-it-is-checked)
- [Not in this change](#not-in-this-change)

## Why

tawk holds one WhatsApp account. A person with two numbers (a personal one and one an AI assistant works on) has to log out and link again to change number, or run a second tawk beside the first with its own folders. Neither gives one place to read and answer everything, and an agent connected through tawk-mcp cannot tell the numbers apart.

The aim is one tawk, in one window, connected to every account at the same time, with agents allowed onto each account separately.

## What was decided

These were chosen by the owner before planning and are not open in this plan.

| Question | Decision |
|---|---|
| How accounts live in tawk | All accounts connected at once in one process and one window |
| Database | One `tawk.db`; every table that holds account data gains an account column, with a migration of the rows already there |
| tawk-mcp | One server. Every tool that reaches tawk takes an optional `account`; `list_accounts` shows them; channel events say which account a message came from |
| tawk-mcp memory | One shared file. Voices and people are shared; what was learned is tagged with the account it was learned on |
| The same contact on two numbers | A setting keeps that contact as one merged chat or as separate chats, with a default for all chats and a choice per chat |
| Which number sends | A primary account you choose, and a sending account you can set for any one contact. The input shows the sending account and one key changes it for that message |
| A contact's own settings | Set from the contact card: the sending account, merging, and whether agents may answer that chat by themselves |
| Agents | Access is set per account. A new account is invisible to agents until it is switched on. The self-approval chats are per account |
| Chat list | Every account's chats together, each row with an account badge. The header filters to one account |
| Delivery | One pass on the feature branch, reviewed at the end |
| Branches | `feature/multi-account` from `main`, no `develop` |

## What it looks like when done

- **Settings, Accounts** lists the accounts with their label, number, connection state and agent access. From there: add an account (a label, then the linking wizard for that account only), rename, make primary, set agent access, log out, remove.
- The **header** shows an account filter: `All`, or one account. Each chat row carries a short badge in the account's colour. The terminal title and the unread tally count every account.
- A contact who writes to two of your numbers shows as **one chat** when merging is on. Their messages appear in time order, each marked with the number it arrived on. The input says `as <label>` and Alt+A changes the sending number for that message; clicking the label opens "send this message as" and "always send to this person from".
- The **contact card** has a section for that chat: send from, merge across my numbers, and a switch per account for agents answering by themselves.
- A background account that loses its connection shows in the header and in Settings, Accounts. It does not take over the screen.
- `tawk send --account work "Mom" "on my way"`, and `tawk unread` and `tawk tail` name the account.
- An agent calls `list_accounts`, passes `account` to a tool, and receives `account` on every channel event. It sees only the accounts switched on for agents.
- An existing install opens as before: its chats, login and settings become account 1, labelled `main`, and nothing else changes until a second account is added.

## The shape of the change

Nothing in tawk knows about accounts today: no table, store, manager, event, notification or control frame carries one, and the in-process backend holds a single global session. Three ideas keep the change contained.

**1. Stores are bound to an account when they are made.** `sqlite_message_store_create(db, account_id)` returns a store whose every query carries that account. The store contracts (`IMessageStore`, `IChatStore` and the rest) do not change, so the managers that use them do not change either. Each account gets its own caching decorators, so no cache key needs an account.

**2. An account runtime.** The seven managers that hold one gateway or one account's stores (messaging, account, profile, status, status feed, scheduling, call) are built once per account, together with that account's gateway, event queue, composite observer and network monitor. Identity stays implicit in which runtime drained an event, so `Event` needs no account field.

**3. The clients put accounts together.** Managers never call each other, so everything that spans accounts (the unified chat list, merged conversations, totals, the title) is assembled in the terminal client and the control client, from rules that live in engines.

```mermaid
flowchart TD
    MAIN["src/main.c<br/>composition root"]
    HOST["AccountHost<br/>builds and owns the runtimes"]
    R1["AccountRuntime 1<br/>gateway, queue, 7 managers,<br/>stores bound to account 1"]
    R2["AccountRuntime 2<br/>gateway, queue, 7 managers,<br/>stores bound to account 2"]
    ROSTER["AccountRosterManager<br/>add, rename, primary, agent access, remove"]
    DB[("tawk.db<br/>account_id on every account table")]
    TUI["Terminal client<br/>unified list, merged chats, switcher"]
    CTL["Control client<br/>account on every op and event"]
    MCP(["tawk-mcp"])

    MAIN -.->|creates and injects| HOST & ROSTER & TUI & CTL
    HOST --> R1 & R2
    R1 & R2 --> DB
    ROSTER --> DB
    TUI -->|IAccountDirectory| HOST
    CTL -->|IAccountDirectory| HOST
    TUI --> ROSTER
    MCP --> CTL
```

## Work, in order

The order is the order of dependency. tawk builds with no warnings and passes its tests after each numbered step, and each is its own commit.

### 1. The account and the database

**Core**
- `core/account.h`: `Account { id, label, jid, name, colour, is_primary, agent_access, created_at }`. `id` is a small integer that never changes and is never reused; the label is the person's and can be renamed.
- `core/account_id.h`: the `AccountId` type and `ACCOUNT_ID_FIRST` (1).
- `core/chat_merge_choice.h`: `CHAT_MERGE_FOLLOW`, `CHAT_MERGE_ON`, `CHAT_MERGE_OFF` for the per-chat choice.

**Migration 17**, in `src/resource_access/sqlite_database.c`. SQLite cannot change a primary key, so the tables are rebuilt in one transaction: create the new table, copy, drop, rename.

| Table | New key | Notes |
|---|---|---|
| `accounts` | `id` | New. One row is inserted for what is already there: id 1, label `main`, primary |
| `messages` | `(account_id, id)` | `rowid` is copied as it is, because the search index is tied to it |
| `chats` | `(account_id, jid)` | |
| `contacts` | `(account_id, jid)` | |
| `profiles` | `(account_id, jid)` | |
| `jid_aliases` | `(account_id, alias)` | |
| `reactions` | `(account_id, message_id, sender_jid)` | |
| `message_receipts` | `(account_id, message_id, jid)` | |
| `statuses` | `(account_id, id)` | |
| `scheduled_messages` | `id`, plus `account_id` | Ids are made locally and stay unique |
| `automation_log` | `id`, plus `account_id` | Added with `ALTER TABLE` |
| `chat_prefs` | `jid` | New. What you chose for a person or group, whichever account they are on: the sending account and the merge choice. It has no account column because it spans them |

Existing rows get `account_id = 1`. The same message reaches two accounts in one group with the same id and chat, which is why the id alone can no longer be the key.

Details that the migration has to get right:
- The base schema runs on every open with `IF NOT EXISTS`. It is changed to run only on a database at version 0, so it cannot bring back an old index.
- The three search triggers go with the old `messages` table. `ensure_search_index` already rebuilds the index when the insert trigger is missing; it is changed to do that after any migration that rebuilt `messages`, and the search query gains a join on `account_id`.
- `keep_copy_before_upgrade` writes `tawk.db.pre-v17` first, as it does for every upgrade, with the same key when the database is encrypted.

**Stores.** Every `sqlite_*_store_create` gains an `AccountId`. Each statement gains `account_id = ?`, and each `ON CONFLICT` target becomes the composite key (`sqlite_message_store.c`, `sqlite_chat_store.c`, `sqlite_contact_store.c`, `sqlite_reaction_store.c`, `sqlite_receipt_store.c`, `sqlite_status_store.c`, `sqlite_jid_alias_store.c`, `sqlite_profile_store.c`, `sqlite_scheduled_message_store.c`, `sqlite_automation_log.c`). Whole-table operations become account-wide: `chats.get_all`, `set_blocklist`, `statuses.authors` and `prune`, `scheduled.due`, the `reassign_*` and `merge` calls, and the alias table loaded at create.

**New**
- `contracts/i_chat_prefs_store.h` and `resource_access/sqlite_chat_prefs_store.c`: get and set a contact's sending account and merge choice, list every contact with a sending account of its own, and clear the ones that point at an account being removed.
- `contracts/i_account_store.h` and `resource_access/sqlite_account_store.c`: list, get, add, rename, set primary, set agent access, set jid and name, set last chat, remove (which deletes that account's rows from every table in one transaction).
- `engines/account_label_validator.c`: a label is 1 to 24 characters, letters, digits, space, dash; unique without regard to case.
- `managers/account_roster_manager.c`: the use cases over `IAccountStore` and the validator. Exactly one account is primary; removing the primary passes it to the lowest id left; the last account cannot be removed.

**Tests**
- `migration_test.c` is rewritten to build its old database from a literal schema (today it makes one by dropping columns from a current database, which would no longer be an old database), and checks that every row lands in account 1, that the search index answers after the rebuild, and that a failed migration leaves version 16 untouched.
- New `account_store_test.c`: two stores over one database do not see each other's rows; the same message id in two accounts is two rows; removing an account leaves the other whole.
- New `account_roster_test.c`: labels, primary, removal.

### 2. Several sessions in the backend

The Go bridge keeps one global session, starting a second silently drives the first, and events are emitted by free functions that know no session.

**Go bridge** (`bridge/whatsmeow`)
- `exports.go`: a registry of sessions by handle. `TawkWmInit(handle, json)`, `TawkWmCommand(handle, json)`, `TawkWmShutdown(handle)`.
- `emit` becomes a method on `Session` and passes its handle to `tawk_wm_emit(handle, line)`. About 86 call sites change, with the free helpers `emitClosed`, `profileUpdated` and `emitAlias`.
- The debug log becomes `whatsmeow-<handle>.log`.

**C gateways**
- `whatsmeow_gateway.c`: each gateway holds its handle and uses it in every call. The single static event queue becomes a small table from handle to queue behind the existing lock, since the callback from Go arrives without a `self`.
- `sidecar_gateway.c` already keeps everything per instance. It only needs its own auth folder and log file per account.

**Paths.** Account 1 keeps `<data_dir>/auth`, so an existing login is not moved. Account N uses `<data_dir>/accounts/<N>/auth`. Media stays in one `media_dir`: files are named by message id and by JID, and the same message downloaded by two accounts is the same file.

**Test.** `connection_recovery_test.c` gains a case with two gateways over the Baileys fake, each receiving only its own events. The whatsmeow side is checked by hand with two real numbers (see [How it is checked](#how-it-is-checked)).

### 3. One runtime per account

- `include/composition/account_runtime.h`, `src/composition/account_runtime.c`: builds one account's gateway, event queue, bound stores with their caches, network monitor, composite observer and the seven managers, in the order `main.c` uses today, and destroys them in reverse.
- `include/contracts/i_account_directory.h`: what the clients may ask: how many runtimes, a runtime's managers by account id, add a runtime for a new account, remove one. `src/composition/account_host.c` implements it and owns the runtimes (at most 8).
- `src/main.c` builds the shared parts as now (settings, themes, audio, notifier, automation, control transport), then the `AccountHost`, then one runtime per row in `accounts`.
- `MAX_OBSERVERS` stays 8: each runtime has its own composite observer.
- The frame loop ticks **every** runtime each frame. A background account whose queue is not drained fills it and blocks its backend thread.
- `core/notification.h` gains `account_id` and `account_label`, filled from the runtime's label, so a sink can say which number it was. `BlinkState` gains `account_id`, so a blink for one account does not light the same contact in another.

`composition/` is new. It is part of the composition root and the only other place that may construct implementations; ARCHITECTURE.md is updated to say so.

### 4. The terminal client

- `TuiAppDeps` loses its single manager pointers and gains `IAccountDirectory`, `AccountRosterManager` and the active filter. It is initialised by name, since the positional initialiser in `main.c` breaks silently when the struct changes shape.
- `clients/tui/unified_chat_list.c`: builds the rows for the list from every runtime's chats, applying the filter and the merge policy. A row is one chat with the accounts it belongs to.
- `clients/tui/account_badge.c`: draws the badge. The chat list, the conversation header and the message view use it.
- `clients/tui/account_filter.c` and the header: the filter chip beside the name. Click or Alt+number chooses `All` or an account.
- `clients/tui/accounts_dialog.c` with `accounts_dialog_request.h`: the list under Settings, Accounts, modelled on `scheduled_list_dialog.c` (rows with actions and an inline text field). Reached by a new `MENU_ACTION_ACCOUNTS`, as `MENU_ACTION_SELF_APPROVAL_CHATS` is. Removing asks through `ConfirmDialog` with a new `CONFIRM_REMOVE_ACCOUNT`.
- `clients/tui/label_dialog.c`: a plain one-line prompt for a label. `profile_text_dialog.c` is tied to profile fields and is left alone.
- **Linking.** `LoginView` is given the account it is linking. It takes over the screen only when no account is linked; otherwise it opens as a dialog, so adding a second account never hides the first.
- **Outages.** The blocking overlay shows only when every account is down. One account down shows in the header and the accounts dialog.
- **Per-account view state.** The last chat is kept per account in `accounts`, with the last filter in the settings. Reply and edit ids, mention picks and the composer draft are reset or keyed by account when the chat in view changes account.
- **Title and tally.** `update_tab` and the header tally sum `messaging_manager_tally` over the runtimes.
- **Scheduled messages** go out through the runtime of the account they were written on.

**Tests.** New `unified_chat_list_test.c` (order, filter, badges). `account_dialogs_test.c` gains the accounts dialog. `chat_list_fold_test.c` and `chat_list_drag_test.c` run against two accounts.

### 5. Merged chats

- **Setting**: `[chats] merge_accounts`, on or off, default on, in `settings_schema.c`. **Per contact**: follow the setting, always merge, never merge, kept in `chat_prefs.merge` and set from the contact card.
- `engines/chat_merge_policy.c`, no I/O: two chats in different accounts are one when they have the same canonical JID (a person's JID is the same whichever of your numbers they write to; a group is the same group), and the setting with both chats' choices allows it. An explicit "never" on either side wins.
- `engines/reply_account_policy.c`, no I/O. The sending account is, in this order: the one chosen for the message being written; the contact's own sending account, when that account has the chat; the primary account, when it has the chat; else the account with the newest message.
- `clients/tui/merged_message_window.c`: joins the message windows of the accounts in time order. In a group that two of your numbers belong to, each message arrives twice with the same id and is shown once, marked with both accounts.
- Read marks, typing, and unread counts in a merged chat act on every account that has it. Mute, pin, archive and lock are set on all of them together so the row has one state.
- **In the conversation.** The input shows `as <label>`. Alt+A moves to the next account that has the chat, for this message only. Clicking the label, or Alt+Shift+A, opens a short menu: "Send this message as", listing the accounts, and "Always send to <name> from", which saves the contact's sending account. The conversation header carries the badges of the accounts the chat is on.
- **On the contact card.** `clients/tui/chat_prefs_section.c` adds a "This chat" section to the profile view: **Send from** (the primary account, or one named account), **Merge across my numbers** (follow the setting, always, never), and **Agents may answer by themselves**, one switch for each account whose agent access is `admin`, which adds or removes the chat in that account's self-approval list. The card's existing mute, tone and theme stay where they are.
- **In the settings.** Settings, Accounts shows the primary account and opens "Contacts with their own sending number", a list of every contact with a sending account of its own (`clients/tui/send_account_dialog.c`), where each can be changed or put back to the primary. Settings, Chats holds "Merge the same contact across my numbers".
- Removing an account puts every contact that sent from it back to the primary.

**Tests.** New `chat_merge_test.c`: the setting and the three per-contact choices; a group seen twice; unread counts across accounts. New `reply_account_test.c`: each rung of the order above, a contact whose sending account no longer has the chat, and an account removed. New `chat_prefs_store_test.c`.

### 6. The control socket and the shell commands

The protocol stays at version 1: fields are added and none change meaning.

- **hello** gains `accounts: [{id, label, jid, name, connected, access}]`, listing only accounts an agent may use, and `multi_account: true`. The single `account` object stays and names the default account.
- **Every operation** accepts `account` (an id or a label). Without it the operation uses the default account: the primary one if agents may use it, else the lowest id they may use. A chat reference is resolved inside that account. `ambiguous` candidates carry the account.
- **`list_accounts`** is a new read operation.
- **Events** (`message`, `read`, `reaction`, `edit`, `delete`, `scheduled_sent`, `chat`) carry `account`. The live cursor is kept per account.
- **Access per account.** `engines/automation_policy.c` takes the account's access. The existing `[automation] access` is copied to account 1 by the migration and stops being read; a new account starts at `off`. `self_approval_chats` moves to the account. `chats`, the rate limits and the disclaimer stay global.
- **Approvals and the log.** `ApprovalRequest`, `AutomationEntry`, `ControlPending`, `ControlSession` subscriptions and `ControlWatch` carry the account. The Agentic tab shows it in the queue, the log and the detail.
- **Shell commands** (`clients/cli/control_command.c`, `control_options.h`): `--account NAME`. `tail` and `unread` print the account; `status-line` sums over accounts.

**Tests.** `control_protocol_test.c` gains: an agent sees only accounts switched on; an operation without `account` lands on the default; the same JID in two accounts is told apart; events are tagged; a self-approval chat allowed in one account is refused in another.

### 7. tawk-mcp

On `feature/multi-account` in `tawk-mcp`, after tawk's control socket is done.

- **Reading the accounts.** `HelloInfo` gains `Accounts` and `MultiAccount`. A `list_accounts` tool on `AppTools` and `IAppManager`.
- **Old tawk.** If the hello has no `multi_account`, a tool call that names any account but the default is refused with a clear message. An old tawk would ignore the field and send from the wrong number, which is the worst failure this change can cause.
- **The `account` argument.** Each of the 38 tools that reach tawk, and the memory tools that resolve a chat, gains an optional `account`, placed before `progress` and `cancellationToken`, described once in `ToolText`.
- **Carrying it.** `IAccountScope` in Core, bound in `TawkMcpComposition`. `ToolResults.RunAsync` opens the scope for the call; an `ITawkControl` decorator, `AccountScopedTawkControl`, adds `account` to each request. No manager interface changes, and the fake control sees the account in the request's arguments. The alternative, an explicit parameter on every manager method, the gate and the control interface, touches every interface and fake for the same result; the scope is one injected service with one implementation.
- **Outside a tool call** there is no scope, so `subscribe` and `approve` name the account themselves. A parked request stores its account (`ParkedRequests`, `WaitingRequest`), `approve` sends it, and `list_pending` shows it.
- **Events.** `TawkEvent` gains `Account`, set in one place in `ControlLineCodec`. `ChannelEventSink` adds `account` to the meta, `EventStreamHub` to each event, `NotificationFormatter` to the header (flattened like a name, since a label is the person's text). `ChannelContextHints` and the resource subscriptions are keyed by account and JID.
- **Resources and prompts.** `tawk://{account}/chat/{jid}` and `tawk://{account}/chats` beside the existing forms, which mean the default account. `catch_up` and `draft_reply` name the account.
- **Instructions.** A paragraph in `TawkServerInstructions`: accounts exist, pass the `account` an event arrived with when answering it, and never move a conversation to another number unasked.
- **Memory, schema step 3.** A nullable `account` on `contact_fact` and `okf_link`; for `okf_concept` the tag goes in `extra` through `ObservationDetails`, so export, import and sync carry it. The matching `SyncTables` columns. Keys stay as they are, so a person known on two numbers is one profile. The tag is the account's JID, which is stable, since the offline commands cannot ask tawk for a label.
- **Tests.** Codec, hello, the account on requests through the decorator, the old-tawk refusal, the channel meta, parked approvals, schema step 3, sync with the new column.

### 8. Encryption, backup, doctor and logs

- **Encryption** is unchanged: one file, one passphrase. `same_contents` already compares row counts table by table and holds after the rebuild.
- **Backup and restore**: the manifest gains the list of accounts and moves to format 2 (format 1 still restores, as account 1). `auth` is copied as a tree, and `accounts/<N>/auth` is added to the copy and to the restore path policy (`engines/archive_path_policy.c`).
- **`tawk --doctor`** lists the accounts with their login folder and state.
- **Logs**: one `tawk.log`; lines from a runtime carry its label.

### 9. Documentation

- `tawk`: MANUAL.md (accounts, the filter, merged chats, per-account agent access, with redrawn pictures by `make screenshots`), CONFIGURATION.md (`merge_accounts`, where each account's login lives, what moved out of `[automation]`), CONTROL.md (the added fields and `list_accounts`), ARCHITECTURE.md (the account runtime, `composition/`, the new contracts and engines), HOW_IT_WORKS.md (start-up with several accounts, a merged chat), SECURITY.md (per-account agent access), README.md.
- `tawk-mcp`: README.md tools table, MANUAL.md (the `account` argument, `list_accounts`, the channel tag, resources), CONFIGURATION.md, ARCHITECTURE.md (the tool counts there are already out of date and are recounted), HOW_IT_WORKS.md, SECURITY.md, AGENT.md.
- Both `AGENT_SETUP.md` files: an agent is told that a second number carries the same ban risk as the first and that linking one is the person's decision.

## How the rules are kept

| Rule | Here |
|---|---|
| iDesign layers | Policy with no I/O in engines (`chat_merge_policy`, `reply_account_policy`, `account_label_validator`, the account part of `automation_policy`). Use cases in `AccountRosterManager`. SQL in `sqlite_account_store` behind `IAccountStore`. Everything that spans accounts is put together in the clients, because managers never call each other |
| Single responsibility, one type per file | Each new type above has its own header and source, named after it |
| Open/closed | The store contracts and the seven managers are not edited to learn about accounts; they are given stores that are already bound to one |
| Liskov substitution | Both gateways still satisfy `IMessageGateway` unchanged; only how one is made differs |
| Interface segregation | `IAccountStore` and `IAccountDirectory` are new and small. No existing contract is widened |
| Dependency inversion | The clients depend on `IAccountDirectory`, not on `AccountHost`. Implementations are made only in `main.c` and `composition/` |
| Composition over inheritance | An account runtime is a composition of the existing parts. tawk-mcp adds the account with a decorator over `ITawkControl` |
| Separation of concerns | Which chats merge and which number replies are decided in engines; the client only draws and asks |

## Risks

| Risk | What is done about it |
|---|---|
| The migration damages a real database | The `.pre-v17` copy is written first; the rebuild is one transaction and rolls back whole; the test runs it over a literal old schema with rows in every table, and over an empty one |
| A message goes out from the wrong number | The input always names the sending account; tawk-mcp refuses a named account on a tawk that does not report `multi_account`; a scheduled message keeps the account it was written on |
| An agent reaches the personal number | New accounts start at `off`; hello and `list_accounts` list only accounts switched on; self-approval is per account; the control tests assert each of these |
| whatsmeow misbehaves with two clients in one process | The bridge sets no library globals and each session has its own store. This is proven with two real numbers before the terminal client work starts; if it fails, the fallback for the second account is the Baileys sidecar, which already runs one process per instance |
| A second linked number raises the chance of a ban | WhatsApp treats linking an unofficial client as a breach of its terms. Two numbers from one machine is two such links. The documentation and both `AGENT_SETUP.md` files say so |
| Size | Most of both codebases is touched. Each step in the order above builds and passes its tests alone, so a fault is found in the step that made it |

## How it is checked

1. `make` with no warnings and `make test` after every step; `dotnet build TawkMcp.slnx -warnaserror` and `dotnet test TawkMcp.slnx` for tawk-mcp.
2. **Upgrade**: copy a real `~/.local/share/tawk` aside, open it with the new build, and confirm every chat, the search, the login and the settings are as before, as account `main`.
3. **Two numbers**: add a second account and link it. Both stay connected, each receives only its own messages, and a network change reconnects both.
4. **Merged chat**: a contact writes to both numbers. One chat with merging on, two with it off, and the per-chat choice overrides the setting. Reply from each number and confirm on the phone which one sent.
5. **A shared group**: each message shows once.
6. **Agents**: with tawk-mcp connected, `list_accounts` shows only the account switched on; a send with `account` goes from that number; a channel event carries `account`; the personal account stays invisible while its access is `off`.
7. **Backup and restore** with two accounts, with and without `--with-login`.
8. **Encrypted database**: upgrade one and confirm it opens and the `.pre-v17` copy is encrypted.

## Not in this change

- A separate passphrase or a separate database per account.
- Different themes, notification sounds or privacy settings per account. They stay global.
- Forwarding or moving a chat from one account to another.
- Linking an account from an agent. Linking needs the phone and stays with the person.
