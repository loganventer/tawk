/* Managing messages over the control socket: edits, deletes, forwards,
 * retries and downloads. */
#include "control_server_state.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FORWARD 5

static cJSON *do_edit(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    if (messaging_manager_edit(s->deps.messaging, control_codec_string(p->args, "message_id"), p->text) != 0) {
        str_copy(f->why, sizeof(f->why), "The message can no longer be edited");
        return NULL;
    }
    return cJSON_CreateObject();
}

void control_op_edit_message(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const char *text = control_required(s, session, req, "text");
    if (!text) return;
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    int ok = messaging_manager_can_edit(s->deps.messaging, &msg);
    ControlPending p;
    control_pending_init(&p, req->id, "edit_message", WRITE_KIND_MANAGE, do_edit);
    str_copy(p.chat_jid, sizeof(p.chat_jid), msg.chat_jid);
    cJSON_AddStringToObject(p.args, "message_id", msg.id);
    message_dispose(&msg);
    if (!ok) {
        control_pending_dispose(&p);
        control_fail(s, session->conn, req->id, "failed", "Only your own text messages can be edited, within 15 minutes");
        return;
    }
    p.text = str_dup(text);
    p.editable = 1;
    p.needs_connection = 1;
    str_copy(p.action, sizeof(p.action), "change your message to");
    control_write(s, session, &p);
}

static cJSON *do_delete(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    int everyone = control_codec_bool(p->args, "for_everyone", 0);
    if (messaging_manager_delete(s->deps.messaging, control_codec_string(p->args, "message_id"), everyone) != 0) {
        str_copy(f->why, sizeof(f->why), "The message could not be deleted");
        return NULL;
    }
    return cJSON_CreateObject();
}

void control_op_delete_message(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    int everyone = control_codec_bool(req->args, "for_everyone", 0);
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    if (everyone && !messaging_manager_can_delete_for_everyone(s->deps.messaging, &msg)) {
        message_dispose(&msg);
        control_fail(s, session->conn, req->id, "failed", "Only your own recent messages can be deleted for everyone");
        return;
    }
    ControlPending p;
    control_pending_init(&p, req->id, "delete_message", WRITE_KIND_DESTRUCTIVE, do_delete);
    str_copy(p.chat_jid, sizeof(p.chat_jid), msg.chat_jid);
    cJSON_AddStringToObject(p.args, "message_id", msg.id);
    cJSON_AddBoolToObject(p.args, "for_everyone", everyone);
    char preview[160];
    messaging_manager_message_preview(s->deps.messaging, &msg, preview, sizeof(preview));
    snprintf(p.action, sizeof(p.action), "delete \"%.80s\" %s", preview, everyone ? "for everyone" : "for you");
    p.needs_connection = everyone;
    message_dispose(&msg);
    control_write(s, session, &p);
}

static cJSON *do_forward(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    const char *jids[MAX_FORWARD];
    int count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(p->args, "jids")) {
        if (count < MAX_FORWARD && cJSON_IsString(item)) jids[count++] = item->valuestring;
    }
    int n = messaging_manager_forward(s->deps.messaging, control_codec_string(p->args, "message_id"), jids, count);
    if (n < 0) { str_copy(f->why, sizeof(f->why), "The message could not be forwarded"); return NULL; }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "forwarded", n);
    return r;
}

void control_op_forward_message(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const cJSON *chats = cJSON_GetObjectItemCaseSensitive(req->args, "chats");
    int wanted = cJSON_IsArray(chats) ? cJSON_GetArraySize(chats) : 0;
    if (wanted < 1 || wanted > MAX_FORWARD) {
        control_fail(s, session->conn, req->id, "bad_request", "\"chats\" is a list of one to five chats");
        return;
    }
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    ControlPending p;
    control_pending_init(&p, req->id, "forward_message", WRITE_KIND_MANAGE, do_forward);
    cJSON_AddStringToObject(p.args, "message_id", msg.id);
    cJSON *jids = cJSON_AddArrayToObject(p.args, "jids");
    char names[256] = "";
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    const cJSON *item;
    cJSON_ArrayForEach(item, chats) {
        int found = -1;
        if (!cJSON_IsString(item) ||
            automation_manager_resolve(s->deps.automation, all, n, item->valuestring, &found, NULL, 0, NULL) != CHAT_RESOLUTION_FOUND) {
            char why[256];
            snprintf(why, sizeof(why), "\"%s\" is not one chat you may see", cJSON_IsString(item) ? item->valuestring : "?");
            control_pending_dispose(&p);
            message_dispose(&msg);
            control_fail(s, session->conn, req->id, "not_found", why);
            return;
        }
        cJSON_AddItemToArray(jids, cJSON_CreateString(all[found].jid));
        size_t used = strlen(names);
        snprintf(names + used, sizeof(names) - used, "%s%s", used ? ", " : "", all[found].name);
    }
    char preview[120];
    messaging_manager_message_preview(s->deps.messaging, &msg, preview, sizeof(preview));
    snprintf(p.action, sizeof(p.action), "forward \"%.50s\" to %.60s", preview, names);
    p.needs_connection = 1;
    message_dispose(&msg);
    control_write(s, session, &p);
}

static cJSON *do_retry(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    if (messaging_manager_retry_message(s->deps.messaging, control_codec_string(p->args, "message_id")) != 0) {
        str_copy(f->why, sizeof(f->why), "The message could not be sent again");
        return NULL;
    }
    return cJSON_CreateObject();
}

void control_op_retry_message(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    int failed = msg.from_me && msg.status == MESSAGE_STATUS_FAILED;
    ControlPending p;
    control_pending_init(&p, req->id, "retry_message", WRITE_KIND_SEND, do_retry);
    str_copy(p.chat_jid, sizeof(p.chat_jid), msg.chat_jid);
    cJSON_AddStringToObject(p.args, "message_id", msg.id);
    if (msg.text) p.text = str_dup(msg.text);
    message_dispose(&msg);
    if (!failed) {
        control_pending_dispose(&p);
        control_fail(s, session->conn, req->id, "failed", "Only your own messages that failed can be sent again");
        return;
    }
    str_copy(p.action, sizeof(p.action), "send a failed message again");
    p.needs_connection = 1;
    control_write(s, session, &p);
}

/* Fetching a photo or video changes nothing, so it is a read. */
void control_op_download_media(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    Message msg;
    if (control_load_message(s, session, req, "message_id", &msg) != 0) return;
    /* A file that is already here is named in the answer: {"path","type","chat":{jid,name}}.
     * One still to fetch answers {} and a media_ready event follows. */
    if (msg.media_path[0] && path_is_regular_file(msg.media_path)) {
        cJSON *r = cJSON_CreateObject();
        cJSON_AddStringToObject(r, "path", msg.media_path);
        cJSON_AddStringToObject(r, "type", message_type_name(msg.type));
        int n = 0;
        const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
        for (int i = 0; i < n; i++) {
            if (strcmp(all[i].jid, msg.chat_jid) != 0) continue;
            cJSON *c = cJSON_AddObjectToObject(r, "chat");
            cJSON_AddStringToObject(c, "jid", all[i].jid);
            cJSON_AddStringToObject(c, "name", all[i].name);
            if (msg.type == MESSAGE_TYPE_AUDIO) control_tag_transcribe(s, r, &all[i]);
            break;
        }
        message_dispose(&msg);
        control_reply(s, session->conn, control_codec_ok(req->id, r));
        return;
    }
    int rc = msg.media_ref ? messaging_manager_fetch_media(s->deps.messaging, msg.id) : -1;
    message_dispose(&msg);
    if (rc != 0) { control_fail(s, session->conn, req->id, "failed", "That message has nothing to download"); return; }
    control_reply(s, session->conn, control_codec_ok(req->id, NULL));
}
