#ifndef APP_CLIENTS_TUI_CHAT_LIST_VIEW_H
#define APP_CLIENTS_TUI_CHAT_LIST_VIEW_H

#include <stdint.h>

#include "clients/tui/account_badge.h"
#include "clients/tui/blink_state.h"
#include "clients/tui/chat_folder.h"
#include "clients/tui/chat_list_entry.h"
#include "clients/tui/image_placement.h"
#include "clients/tui/portrait_source.h"
#include "clients/tui/text_caret.h"
#include "clients/tui/thumbnail_cache.h"
#include "clients/tui/ui_rect.h"
#include "core/chat.h"

#define CHAT_LIST_MAX_ENTRIES 4096
#define CHAT_LIST_MAX_PORTRAITS 24

/* The sidebar: regular chats with shortcuts into the Archived and Locked
 * folders, unread highlighting, pins, mutes, typing, drafts, a search filter
 * and blinking. */
typedef struct ChatListView {
    ChatFolder    folder;
    int           selected;       /* index into entries */
    int           scroll;
    int           filtering;      /* search box has focus */
    char          filter[64];
    TextCaret     caret;          /* the blinking cursor while typing here */
    char          open_jid[128];
    AccountId     open_account;   /* whose chat that is, where several accounts are listed; ACCOUNT_ID_NONE: any */
    int           compact;        /* one row per chat instead of two */
    int           spacing;        /* blank lines between chats, 0 to 2 */
    /* Portraits (detailed style): where pictures come from and how to draw them. */
    const PortraitSource *portraits;       /* NULL: no portraits */
    ThumbnailCache       *thumbs;
    int                   pixel_images;
    ImagePlacement        placements[CHAT_LIST_MAX_PORTRAITS];   /* portraits as pixel images */
    int                   placement_count;
    int           pinned_collapsed;   /* the pinned group is folded away */
    int           others_collapsed;   /* the other chats are folded away */
    /* Dragging a chat into or out of the Pinned group. */
    char          drag_jid[128];      /* the chat under the pressed button; empty when none */
    AccountId     drag_account;       /* and the account its row acts through */
    int           drag_from;          /* the entry the press started on */
    int           dragging;           /* moved off that entry: a drag, not a click */
    int           drop_pinned;        /* where it would land: 1 Pinned, 0 Chats, -1 nowhere */
    /* With several accounts listed, the mark of each, in the order of a row's `accounts` bits. */
    AccountBadge  badges[ACCOUNT_MAX];
    int           badge_count;        /* 0 or 1: rows carry no badge */
    ChatListEntry entries[CHAT_LIST_MAX_ENTRIES];
    int           entry_count;
} ChatListView;

void        chat_list_view_init(ChatListView *view);
/* Rebuilds the entries for the current folder and filter. */
void        chat_list_view_sync(ChatListView *view, const Chat *chats, int count);
void        chat_list_view_render(ChatListView *view, UiRect rect, const Chat *chats, int count,
                                  int focused, int use_24h, const BlinkState *blink, int64_t now_ms);
void        chat_list_view_move(ChatListView *view, int delta);
/* The selected entry when it is a chat, else NULL. */
const Chat *chat_list_view_selected_chat(const ChatListView *view, const Chat *chats);
/* JID of the selected entry when it is a chat, else NULL. */
const char *chat_list_view_selected_jid(const ChatListView *view, const Chat *chats);
/* Enter or click: opens a folder or folds a group (returns NULL), or returns the chat's JID. */
const char *chat_list_view_activate(ChatListView *view, const Chat *chats, int count);
/* Selects the entry under a click row; returns 1 when there is one. */
int         chat_list_view_hit(ChatListView *view, UiRect rect, int y);
void        chat_list_view_filter_key(ChatListView *view, int wide_char);
/* On a group header: folds (expand 0) or unfolds (expand 1) it; returns 1 when it acted. */
int         chat_list_view_fold(ChatListView *view, const Chat *chats, int count, int expand);
/* Leaves the Archived or Locked folder; returns 1 when it did. */
int         chat_list_view_back(ChatListView *view, const Chat *chats, int count);
/* Moves to the next chat with unread messages in the folder; returns its JID or NULL. */
const char *chat_list_view_next_unread(ChatListView *view, const Chat *chats);
/* Drag and drop between the Pinned group and the other chats, in the main
 * list only. begin arms a drag from the selected chat (returns 1 when armed);
 * move follows the pointer, showing an empty Pinned group while dragging so
 * the first chat can be pinned too; end returns 1 to pin, 0 to unpin or -1
 * to leave the chat as it is, and copies its JID to `jid`. */
int         chat_list_view_drag_begin(ChatListView *view, const Chat *chats);
void        chat_list_view_drag_move(ChatListView *view, const Chat *chats, int count, UiRect rect, int y);
int         chat_list_view_drag_end(ChatListView *view, const Chat *chats, int count, UiRect rect, int y,
                                    char *jid, unsigned long size);
/* Shows the folder that contains `jid` and selects it. With unfold set, a
 * folded group holding it opens too; otherwise the folding stays as it is. */
void        chat_list_view_reveal(ChatListView *view, const Chat *chats, int count, const char *jid, int unfold);

#endif
