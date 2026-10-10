#ifndef APP_CLIENTS_TUI_MENU_INFO_H
#define APP_CLIENTS_TUI_MENU_INFO_H

/* Live values shown on read-only rows. MENU_INFO_STATIC shows only the title. */
typedef enum MenuInfo {
    MENU_INFO_STATIC = 0,
    MENU_INFO_NAME,
    MENU_INFO_NUMBER,
    MENU_INFO_CONNECTION,
    MENU_INFO_BACKEND,
    MENU_INFO_AUDIO,
    MENU_INFO_CONFIG_PATH,
    MENU_INFO_USER_THEMES,
    MENU_INFO_VERSION,
    MENU_INFO_AUTHOR,
    MENU_INFO_AGENTS,               /* whether the control socket listens, and who is connected */
    MENU_INFO_SELF_CHATS,           /* how many chats an admin agent may answer its own requests in */
    MENU_INFO_VOICE_LANGUAGES       /* the languages voice notes are spoken in, unless a chat names its own */
} MenuInfo;

#endif
