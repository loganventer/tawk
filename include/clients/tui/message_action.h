#ifndef APP_CLIENTS_TUI_MESSAGE_ACTION_H
#define APP_CLIENTS_TUI_MESSAGE_ACTION_H

typedef enum MessageAction {
    MESSAGE_ACTION_REPLY = 0,
    MESSAGE_ACTION_REACT,
    MESSAGE_ACTION_EDIT,
    MESSAGE_ACTION_COPY,
    MESSAGE_ACTION_FORWARD,              /* send a copy to other chats */
    MESSAGE_ACTION_OPEN,
    MESSAGE_ACTION_READ,
    MESSAGE_ACTION_TRANSCRIPT,           /* a voice note's transcript, in full */
    MESSAGE_ACTION_TLDR,                 /* unfold a summarised message to its original, or fold it back */
    MESSAGE_ACTION_RETRY,
    MESSAGE_ACTION_SAVE,                 /* copy the file to the Downloads folder */
    MESSAGE_ACTION_GOTO_QUOTE,           /* scroll to the message a reply quotes */
    MESSAGE_ACTION_INFO,                 /* who received and read a message you sent */
    MESSAGE_ACTION_DELETE,               /* opens the choice below */
    MESSAGE_ACTION_DELETE_FOR_ME,
    MESSAGE_ACTION_DELETE_FOR_EVERYONE,
    MESSAGE_ACTION_CANCEL,
    MESSAGE_ACTION_COUNT
} MessageAction;

const char *message_action_label(MessageAction action);

#endif
