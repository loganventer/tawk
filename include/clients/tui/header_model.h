#ifndef APP_CLIENTS_TUI_HEADER_MODEL_H
#define APP_CLIENTS_TUI_HEADER_MODEL_H

/* What the header bar shows. */
typedef struct HeaderModel {
    const char *user_name;
    const char *status;       /* connection state emoji: 🟢 🟡 🔴 ⚪ */
    int         dnd;
    const char *tally;        /* "💬 3  🖼 1" */
    int         blink_on;
    int         use_24h;
    int         sidebar_open;
    int         show_post;    /* draw ⭕ (statuses) and + (post one); hidden while linking */
    int         unseen_statuses;  /* people with statuses you have not seen */
    int         show_tabs;        /* draw the Chats and Agentic tabs (hidden while linking) */
    int         agents_tab_active;/* the Agentic tab is the one open */
    int         agents_waiting;   /* requests waiting for you */
    int         agents_high;      /* of which HIGH risk */
    int         agents_connected; /* programs connected now */
    const char *account;          /* which account's chats the list shows: "All" or a label; empty with one account */
} HeaderModel;

#endif
