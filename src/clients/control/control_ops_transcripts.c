/* Voice note transcripts over the control socket. tawk transcribes nothing:
 * a transcriber hands the words over here, and reads back what is kept. */
#include "control_server_state.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

void control_tag_transcribe(ControlServer *s, cJSON *object, const Chat *chat) {
    if (!s->deps.transcripts || !chat) return;
    if (!transcript_manager_transcribing(s->deps.transcripts, chat)) cJSON_AddBoolToObject(object, "transcribe", 0);
}

static int available(ControlServer *s, const ControlSession *session, const ControlRequest *req) {
    if (s->deps.transcripts) return 1;
    control_fail(s, session->conn, req->id, "failed", "This tawk keeps no transcripts");
    return 0;
}

/* Keeping a transcript changes only this computer and sends nothing, so it
 * is not asked about and does not count as a write. It reaches only a
 * message whose chat the client may see, and only a chat that is transcribed. */
void control_op_set_transcript(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!available(s, session, req)) return;
    const char *text = control_required(s, session, req, "text");
    if (!text) return;
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    const Chat *chat = control_visible_chat(s, msg.chat_jid);
    char source[64];
    str_copy(source, sizeof(source), session->client);
    TranscriptSaveResult result = transcript_manager_save(s->deps.transcripts, &msg, chat, control_codec_string(req->args, "language"),
                                                          text, control_codec_string(req->args, "model"), source);
    char chat_jid[128];
    str_copy(chat_jid, sizeof(chat_jid), msg.chat_jid);
    message_dispose(&msg);
    const char *why = transcript_manager_error(s->deps.transcripts);
    AutomationOutcome outcome = result == TRANSCRIPT_SAVED ? AUTOMATION_OUTCOME_DONE
                              : result == TRANSCRIPT_FAILED ? AUTOMATION_OUTCOME_FAILED : AUTOMATION_OUTCOME_REFUSED;
    automation_manager_record(s->deps.automation, session->origin, session->client, req->op, chat_jid,
                              result == TRANSCRIPT_SAVED ? "a voice note's transcript" : why, outcome);
    switch (result) {
        case TRANSCRIPT_SAVED:
            s->changed = 1;
            control_reply(s, session->conn, control_codec_ok(req->id, NULL));
            break;
        case TRANSCRIPT_OFF:     control_fail(s, session->conn, req->id, "transcripts_off", why); break;
        case TRANSCRIPT_REFUSED: control_fail(s, session->conn, req->id, "bad_request", why); break;
        default:                 control_fail(s, session->conn, req->id, "failed", why); break;
    }
}

#define WANTED_PER_TICK 2

/* The first connected agent that is not paused: any of them can transcribe, and one is enough. */
static int transcriber(ControlServer *s) {
    for (int i = 0; i < s->session_count; i++) {
        const ControlSession *c = &s->sessions[i];
        if (c->greeted && c->origin == CONTROL_ORIGIN_MCP && !c->paused) return c->conn;
    }
    return -1;
}

/* Older voice notes you looked at that have no transcript: {"evt":"transcript_wanted","chat":{jid,name},"message_id"}
 * to one agent, which transcribes them as it does the ones that arrive. */
void control_transcripts_tick(ControlServer *s) {
    int accounts = control_account_count(s);
    for (int a = 0; a < accounts; a++) {
        if (control_serve_account(s, control_account_at(s, a)) != 0) continue;
        TranscriptManager *mgr = s->deps.transcripts;
        char id[64];
        for (int k = 0; mgr && k < WANTED_PER_TICK && transcript_manager_next_wanted(mgr, id, sizeof(id)); k++) {
            int conn = transcriber(s);
            if (conn < 0) return;                           /* nobody to do it yet: they wait */
            Message msg;
            if (messaging_manager_get(s->deps.messaging, id, &msg) != 0) { transcript_manager_drop_wanted(mgr); continue; }
            const Chat *chat = control_visible_chat(s, msg.chat_jid);
            if (chat && transcript_manager_transcribing(mgr, chat)) {
                cJSON *evt = cJSON_CreateObject();
                cJSON *c = cJSON_AddObjectToObject(evt, "chat");
                cJSON_AddStringToObject(c, "jid", chat->jid);
                cJSON_AddStringToObject(c, "name", chat->name);
                cJSON_AddStringToObject(evt, "message_id", id);
                control_tag_account(s, evt);
                control_reply(s, conn, control_codec_event("transcript_wanted", evt));
            }
            message_dispose(&msg);
            transcript_manager_drop_wanted(mgr);
        }
    }
}

/* What is kept for a message: {"transcripts":[{"language","text","model","at"}]}, newest first. */
void control_op_get_transcript(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!available(s, session, req)) return;
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    Transcript *all = NULL;
    int n = 0;
    transcript_manager_find(s->deps.transcripts, msg.id, &all, &n);
    message_dispose(&msg);
    cJSON *r = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(r, "transcripts");
    for (int i = 0; i < n; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "language", all[i].language);
        cJSON_AddStringToObject(t, "text", all[i].text ? all[i].text : "");
        cJSON_AddStringToObject(t, "model", all[i].model);
        cJSON_AddNumberToObject(t, "at", (double)all[i].created_at);
        cJSON_AddItemToArray(list, t);
    }
    transcript_array_free(all, n);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}
