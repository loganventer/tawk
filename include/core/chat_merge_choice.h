#ifndef APP_CORE_CHAT_MERGE_CHOICE_H
#define APP_CORE_CHAT_MERGE_CHOICE_H

/* Whether one contact's chats in several of your accounts show as one chat. */
typedef enum ChatMergeChoice {
    CHAT_MERGE_FOLLOW = 0,     /* as the setting says */
    CHAT_MERGE_ALWAYS,
    CHAT_MERGE_NEVER
} ChatMergeChoice;

#endif
