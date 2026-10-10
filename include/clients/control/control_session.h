#ifndef APP_CLIENTS_CONTROL_CONTROL_SESSION_H
#define APP_CLIENTS_CONTROL_CONTROL_SESSION_H

#include <stdint.h>

#include "clients/control/control_allowance.h"
#include "clients/control/control_summary_wait.h"
#include "clients/control/control_watch.h"
#include "core/control_origin.h"

#define CONTROL_SESSION_CHATS      64
#define CONTROL_SESSION_ALLOWANCES 16
#define CONTROL_SESSION_WAITS      8

/* One connected client: who it said it is and what it subscribed to. */
typedef struct ControlSession {
    int           conn;
    int           greeted;                 /* hello done */
    ControlOrigin origin;
    char          client[64];
    char          doing[160];      /* what the agent says it is working on; "" until it says */
    char          label[64];       /* what it says tells it apart from others of the same name */
    int           subscribed;
    int           all_chats;               /* subscribed to every chat it may see */
    char          chats[CONTROL_SESSION_CHATS][128];
    int           chat_count;
    ControlWatch *watches;                 /* owned */
    int           watch_count;
    int64_t       since;                   /* epoch seconds */
    int           requests;
    int           paused;                  /* you paused it in the Agents tab */
    int           summariser;              /* you chose it to write TL;DR summaries */
    int           can_transcribe;          /* it said it transcribes voice notes tawk asks about */
    int           can_summarise;           /* it said it writes summaries tawk asks for */
    char          version[24];             /* the client's own version, as it said in hello */
    /* The summaries it was asked for and has not handed back, how many it ever handed back, and how
     * many it let go unanswered: one that answers none is not asked again. */
    ControlSummaryWait waits[CONTROL_SESSION_WAITS];
    int           wait_count;
    int           summaries_answered;
    int           summaries_missed;
    int64_t       passed_over_until_ms;    /* not asked for summaries until then: it let several go unanswered */
    ControlAllowance allowances[CONTROL_SESSION_ALLOWANCES];
    int           allowance_count;
} ControlSession;

void control_session_init(ControlSession *session, int conn);
void control_session_dispose(ControlSession *session);
int  control_session_follows(const ControlSession *session, const char *jid);
int  control_session_allows(const ControlSession *session, const char *op, const char *chat_jid);
void control_session_allow(ControlSession *session, const char *op, const char *chat_jid);

#endif
