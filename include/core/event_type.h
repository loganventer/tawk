#ifndef APP_CORE_EVENT_TYPE_H
#define APP_CORE_EVENT_TYPE_H

typedef enum EventType {
    EVENT_NONE = 0,
    EVENT_MESSAGE_UPSERT,
    EVENT_MESSAGE_STATUS,
    EVENT_MESSAGE_RECEIPT,  /* id, jid (who), receipt, at: one recipient's receipt for a message you sent */
    EVENT_CHAT_UPDATE,
    EVENT_CONTACT_UPDATE,
    EVENT_AUTH_QR,
    EVENT_AUTH_PAIRING_CODE,
    EVENT_AUTH_REQUIRED,
    EVENT_AUTH_CONNECTED,
    EVENT_AUTH_LOGGED_OUT,
    EVENT_CONNECTION_STATUS,
    EVENT_MEDIA_READY,
    EVENT_JID_ALIAS,
    EVENT_REACTION,
    EVENT_MESSAGE_EDIT,     /* message.id, message.chat_jid, message.text, message.deleted */
    EVENT_MESSAGE_REMOVED,  /* message.id, message.chat_jid: deleted for me on another device */
    EVENT_CHAT_REMOVED,     /* chat.jid: the whole chat was deleted on another device */
    EVENT_PROFILE,          /* profile: details of a contact or group */
    EVENT_PICTURE,          /* jid, path (empty with none), full, none */
    EVENT_PICTURE_CHANGED,  /* jid: its profile picture changed */
    EVENT_BLOCKLIST,        /* list: blocked JIDs, one per line */
    EVENT_CALL,             /* id, jid (caller), state (offer, accepted, ended), video, group */
    EVENT_PROFILE_UPDATED,  /* reason (name, about or picture), ok, detail, name: your own profile edit finished */
    EVENT_STATUS_POSTED,    /* id, ok, detail: a status you posted went out or failed */
    EVENT_LINK_PREVIEW,     /* message.id, message.link, message.thumbnail: the card made for a message you sent */
    EVENT_TYPING,
    EVENT_PRESENCE,         /* jid, state (online or offline), at (last seen, 0 when not shared) */
    EVENT_SIDECAR_EXITED,
    EVENT_ERROR
} EventType;

#endif
