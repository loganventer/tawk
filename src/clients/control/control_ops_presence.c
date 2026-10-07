#include "control_server_state.h"

#include "core/contact_presence.h"

/* presence: whether the person in a chat is online, and when they were last
 * seen. It answers with what tawk knows now and asks WhatsApp to keep telling
 * it, as opening the chat would, so a second call a moment later has the
 * answer. {"chat":chat,"state":"online"|"offline"|"unknown","watching":bool},
 * with "last_seen" when they share it. "watching" is false while tawk is not
 * shown as online itself, when WhatsApp tells it nothing. */
void control_op_presence(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!automation_manager_presence_lookup(s->deps.automation, session->origin)) {
        control_fail(s, session->conn, req->id, "not_allowed",
                     "Looking up who is online is switched off in tawk (Settings, Automation, Look up online status)");
        return;
    }
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    int at = control_resolve_chat(s, session, req, "chat", all, n);
    if (at < 0) return;
    Chat chat = all[at];
    if (chat.is_group) { control_fail(s, session->conn, req->id, "bad_request", "Only a person has an online status, not a group"); return; }
    ContactPresence known;
    int have = messaging_manager_presence(s->deps.messaging, chat.jid, &known);
    int watching = messaging_manager_watch_presence(s->deps.messaging, chat.jid);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "chat", control_codec_chat(&chat));
    cJSON_AddStringToObject(r, "state", have ? presence_state_name(known.state) : "unknown");
    if (have && known.last_seen > 0) cJSON_AddNumberToObject(r, "last_seen", (double)known.last_seen);
    cJSON_AddBoolToObject(r, "watching", watching);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}
