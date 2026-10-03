#ifndef APP_CLIENTS_TUI_MENU_ACTION_H
#define APP_CLIENTS_TUI_MENU_ACTION_H

typedef enum MenuAction {
    MENU_ACTION_NONE = 0,
    MENU_ACTION_LOGOUT,
    MENU_ACTION_TEST_SOUND,
    MENU_ACTION_TEST_NOTIFICATION,
    MENU_ACTION_RUN_SCREENSAVER,
    MENU_ACTION_RELOAD_THEMES,
    MENU_ACTION_CLEAR_LOGS,
    MENU_ACTION_RETRY_CONNECTION,
    MENU_ACTION_SELF_APPROVAL_CHATS,  /* choose the chats an admin agent may answer its own requests in */
    MENU_ACTION_ACCOUNTS              /* your accounts: add, name, link and remove them */
} MenuAction;

#endif
