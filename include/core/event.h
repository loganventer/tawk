#ifndef APP_CORE_EVENT_H
#define APP_CORE_EVENT_H

#include "core/contact_profile.h"
#include "core/chat.h"
#include "core/contact.h"
#include "core/event_type.h"
#include "core/message.h"
#include "core/receipt_kind.h"

/* A decoded sidecar event. Fields are populated according to type; owned
 * heap fields (message text, qr) are released by event_dispose. */
typedef struct Event {
    EventType     type;
    Message       message;      /* MESSAGE_UPSERT */
    int           live;         /* MESSAGE_UPSERT: 1 when real time, 0 for history sync */
    Chat          chat;         /* CHAT_UPDATE */
    Contact       contact;      /* CONTACT_UPDATE */
    char          id[64];       /* MESSAGE_STATUS, MEDIA_READY */
    MessageStatus status;       /* MESSAGE_STATUS */
    ReceiptKind   receipt;      /* MESSAGE_RECEIPT */
    int64_t       at;           /* MESSAGE_RECEIPT: when, epoch seconds; PRESENCE: last seen */
    char          path[512];    /* MEDIA_READY */
    char          code[32];     /* AUTH_PAIRING_CODE */
    char         *qr_ascii;     /* AUTH_QR, owned */
    char          jid[128];     /* AUTH_CONNECTED; JID_ALIAS: the phone-number JID; MESSAGE_RECEIPT: who; PRESENCE: the contact */
    char          lid[128];     /* JID_ALIAS: the hidden-user (LID) JID */
    char          emoji[32];    /* REACTION: the emoji, "" when removed */
    char          state[16];    /* TYPING: composing, recording or paused; PRESENCE: online or offline */
    char          name[128];    /* AUTH_CONNECTED */
    char          detail[256];  /* CONNECTION_STATUS, ERROR: human readable reason */
    char          reason[32];   /* CONNECTION_STATUS: open, closed, restart_required, logged_out */
    ContactProfile *profile;    /* PROFILE, owned */
    char         *list;         /* BLOCKLIST, owned */
    int           full;         /* PICTURE: the full-size picture */
    int           none;         /* PICTURE: there is no picture */
    int           video;        /* CALL: a video call */
    int           group;        /* CALL: a group call */
    int           ok;           /* PROFILE_UPDATED, STATUS_POSTED: it worked */
} Event;

void event_init(Event *evt, EventType type);
void event_dispose(Event *evt);

#endif
