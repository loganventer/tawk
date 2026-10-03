#ifndef APP_CLIENTS_TUI_HEADER_HITS_H
#define APP_CLIENTS_TUI_HEADER_HITS_H

#include "clients/tui/ui_rect.h"

/* Where the header drew its clickable parts that move with the text around
 * them, recorded while drawing. Empty (zero width) when not drawn. */
typedef struct HeaderHits {
    UiRect statuses;  /* ⭕: look at statuses */
    UiRect post;      /* + : post a status */
    UiRect profile;   /* your name: view and edit your profile */
    UiRect chats_tab; /* 💬 Chats: back to the chats */
    UiRect agents;    /* 🤖 Agentic: the agents' requests, log and permissions */
    UiRect account;   /* which account's chats are listed: steps to the next */
} HeaderHits;

#endif
