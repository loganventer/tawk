/* TL;DR summaries over the control socket. tawk writes none: an agent's
 * model does, for the long messages of the chats you put in TL;DR mode,
 * and hands each one over here. */
#include "control_server_state.h"
#include "utilities/str_util.h"

#include <string.h>

void control_tag_tldr(ControlServer *s, cJSON *object, const Chat *chat) {
    if (!s->deps.summaries || !chat) return;
    if (summary_manager_tldr(s->deps.summaries, chat->jid) && !chat->soft_locked) cJSON_AddBoolToObject(object, "tldr", 1);
}

static int available(ControlServer *s, const ControlSession *session, const ControlRequest *req) {
    if (s->deps.summaries) return 1;
    control_fail(s, session->conn, req->id, "failed", "This tawk keeps no summaries");
    return 0;
}

/* Keeping a summary changes only this computer and sends nothing, so it is
 * not asked about. It reaches only a text message whose chat the client may
 * see, and only a chat you put in TL;DR mode. */
void control_op_set_summary(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!available(s, session, req)) return;
    const char *text = control_required(s, session, req, "text");
    if (!text) return;
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    const Chat *chat = control_visible_chat(s, msg.chat_jid);
    SummarySaveResult result = summary_manager_save(s->deps.summaries, &msg, chat, text,
                                                    control_codec_string(req->args, "model"), session->client);
    char chat_jid[128];
    str_copy(chat_jid, sizeof(chat_jid), msg.chat_jid);
    message_dispose(&msg);
    const char *why = summary_manager_error(s->deps.summaries);
    AutomationOutcome outcome = result == SUMMARY_SAVED ? AUTOMATION_OUTCOME_DONE
                              : result == SUMMARY_FAILED ? AUTOMATION_OUTCOME_FAILED : AUTOMATION_OUTCOME_REFUSED;
    automation_manager_record(s->deps.automation, session->origin, session->client, req->op, chat_jid,
                              result == SUMMARY_SAVED ? "a message's TL;DR" : why, outcome);
    switch (result) {
        case SUMMARY_SAVED:
            s->changed = 1;
            control_reply(s, session->conn, control_codec_ok(req->id, NULL));
            break;
        case SUMMARY_OFF:     control_fail(s, session->conn, req->id, "tldr_off", why); break;
        case SUMMARY_REFUSED: control_fail(s, session->conn, req->id, "bad_request", why); break;
        default:              control_fail(s, session->conn, req->id, "failed", why); break;
    }
}

/* What is kept for a message: {"summary":{"text","model","at"}}, or {} when it has none. */
void control_op_get_summary(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!available(s, session, req)) return;
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    Summary kept;
    cJSON *r = cJSON_CreateObject();
    if (summary_manager_find(s->deps.summaries, msg.id, &kept) == 0) {
        cJSON *o = cJSON_AddObjectToObject(r, "summary");
        cJSON_AddStringToObject(o, "text", kept.text ? kept.text : "");
        cJSON_AddStringToObject(o, "model", kept.model);
        cJSON_AddNumberToObject(o, "at", (double)kept.created_at);
        summary_dispose(&kept);
    }
    message_dispose(&msg);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}
