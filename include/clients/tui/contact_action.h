#ifndef APP_CLIENTS_TUI_CONTACT_ACTION_H
#define APP_CLIENTS_TUI_CONTACT_ACTION_H

/* What can be done from the contact details panel. */
typedef enum ContactAction {
    CONTACT_ACTION_VIEW_PHOTO = 0,
    CONTACT_ACTION_SEARCH,
    CONTACT_ACTION_OPTIONS,          /* mute, pin, archive, theme, tone */
    CONTACT_ACTION_SOFT_LOCK,
    CONTACT_ACTION_EXPORT,
    CONTACT_ACTION_EXPORT_MEDIA,
    CONTACT_ACTION_BLOCK,
    CONTACT_ACTION_UNBLOCK,
    CONTACT_ACTION_CLEAR,            /* delete the messages, keep the chat */
    CONTACT_ACTION_DELETE,           /* delete the whole chat */
    /* This chat's own settings. Each steps to its next choice; the panel is told what to show for them. */
    CONTACT_ACTION_SEND_FROM,        /* which of your accounts sends to this contact */
    CONTACT_ACTION_MERGE,            /* whether their chats in several accounts show as one */
    CONTACT_ACTION_AGENT_ANSWERS,    /* whether an agent may answer here by itself */
    CONTACT_ACTION_SHOW_TRANSCRIPTS, /* whether the transcripts of its voice notes show */
    CONTACT_ACTION_TRANSCRIBE,       /* whether its voice notes are transcribed from now on */
    CONTACT_ACTION_VOICE_LANGUAGES,  /* the languages its voice notes are spoken in; opens a list of switches */
    CONTACT_ACTION_TLDR,             /* whether its messages show as a summary */
    CONTACT_ACTION_AGENT_RULE,       /* what agents may do in this chat: as the account says, always ask, read only, hidden */
    CONTACT_ACTION_OWNER_CHAT,       /* whether this, your own chat, is your chat with the agent */
    CONTACT_ACTION_COUNT
} ContactAction;

const char *contact_action_label(ContactAction action);
/* Actions that remove or lock things are confirmed in a dialog first. */
int         contact_action_needs_confirmation(ContactAction action);

#endif
