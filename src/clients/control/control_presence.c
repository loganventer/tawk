#include "control_server_state.h"

#include <time.h>

#include "clients/control/control_codec.h"

/* The person in a chat came online or left:
 * {"evt":"presence","chat":{jid,name},"who":{jid,name},"state":"online"|"offline","at"},
 * with "last_seen" when they share it. Only agents hear it, only when the
 * user switched it on, and never about a chat they may not see. */
void control_presence_send(ControlServer *s, const Chat *chat, const LiveMessageRef *ref) {
    if (!chat || chat->is_group || !ref->detail[0] || !automation_manager_chat_allowed(s->deps.automation, chat)) return;
    for (int i = 0; i < s->session_count; i++) {
        ControlSession *session = &s->sessions[i];
        if (!session->greeted || !control_session_follows(session, ref->chat_jid)) continue;
        if (!automation_manager_pushes_event(s->deps.automation, session->origin, LIVE_KIND_PRESENCE)) continue;
        cJSON *evt = cJSON_CreateObject();
        cJSON *c = cJSON_AddObjectToObject(evt, "chat");
        cJSON_AddStringToObject(c, "jid", chat->jid);
        cJSON_AddStringToObject(c, "name", chat->name);
        cJSON *who = cJSON_AddObjectToObject(evt, "who");
        cJSON_AddStringToObject(who, "jid", chat->jid);
        cJSON_AddStringToObject(who, "name", chat->name);
        cJSON_AddStringToObject(evt, "state", ref->detail);
        if (ref->at > 0) cJSON_AddNumberToObject(evt, "last_seen", (double)ref->at);
        cJSON_AddNumberToObject(evt, "at", (double)time(NULL));
        control_tag_account(s, evt);
        control_reply(s, session->conn, control_codec_event("presence", evt));
    }
}
