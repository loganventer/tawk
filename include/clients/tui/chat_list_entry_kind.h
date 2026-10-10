#ifndef APP_CLIENTS_TUI_CHAT_LIST_ENTRY_KIND_H
#define APP_CLIENTS_TUI_CHAT_LIST_ENTRY_KIND_H

typedef enum ChatListEntryKind {
    CHAT_LIST_ENTRY_CHAT = 0,
    CHAT_LIST_ENTRY_ARCHIVED,   /* opens the archived folder */
    CHAT_LIST_ENTRY_LOCKED,     /* opens the locked folder */
    CHAT_LIST_ENTRY_BACK,       /* returns to the regular chats */
    CHAT_LIST_ENTRY_PINNED,     /* header of the pinned group: collapses or expands it */
    CHAT_LIST_ENTRY_OTHERS,     /* header of the other chats, shown when some are pinned */
    CHAT_LIST_ENTRY_NARROWED    /* says what the list is narrowed to; Enter shows every chat again */
} ChatListEntryKind;

#endif
