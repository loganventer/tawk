#ifndef APP_CLIENTS_TUI_AGENTS_PANEL_REQUEST_H
#define APP_CLIENTS_TUI_AGENTS_PANEL_REQUEST_H

/* What a key or click in the Agents tab asks its owner to do. */
typedef enum AgentsPanelRequest {
    AGENTS_REQUEST_NONE = 0,
    AGENTS_REQUEST_REDRAW,
    AGENTS_REQUEST_CLOSE,
    AGENTS_REQUEST_APPROVE,          /* the selected request (or the marked ones), as it is or as edited */
    AGENTS_REQUEST_DECLINE,
    AGENTS_REQUEST_TOO_LONG,         /* its text is too long to edit here */
    AGENTS_REQUEST_DISCONNECT,       /* the selected agent */
    AGENTS_REQUEST_PAUSE,            /* pause or resume it */
    AGENTS_REQUEST_REVOKE,           /* forget its "for this session" allowances */
    AGENTS_REQUEST_SUMMARISER,       /* make it your default agent (it writes TL;DR summaries), or take that back */
    AGENTS_REQUEST_SET_SETTING,      /* a permission changed */
    AGENTS_REQUEST_SELF_CHATS        /* choose the chats an admin agent may answer its own requests in */
} AgentsPanelRequest;

#endif
