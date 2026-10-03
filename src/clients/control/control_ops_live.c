/* Live updates: clients that subscribe hear about new messages in their
 * chats, and about unread counts that change. */
#include "control_server_state.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIVE_PER_TICK 64

static const Chat *find_chat(ControlServer *s, const char *jid) {
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    for (int i = 0; i < n; i++) if (strcmp(all[i].jid, jid) == 0) return &all[i];
    return NULL;
}

/* Remembers the unread counts the client has now, so only changes are sent. */
static void snapshot(ControlServer *s, ControlSession *session) {
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    free(session->watches);
    session->watches = n ? calloc((size_t)n, sizeof(ControlWatch)) : NULL;
    session->watch_count = 0;
    for (int i = 0; i < n && session->watches; i++) {
        if (!control_session_follows(session, all[i].jid) || !automation_manager_chat_allowed(s->deps.automation, &all[i])) continue;
        ControlWatch *w = &session->watches[session->watch_count++];
        str_copy(w->jid, sizeof(w->jid), all[i].jid);
        w->unread = all[i].unread;
    }
}

void control_op_subscribe(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const cJSON *chats = cJSON_GetObjectItemCaseSensitive(req->args, "chats");
    session->all_chats = 0;
    session->chat_count = 0;
    if (cJSON_IsString(chats) && strcmp(chats->valuestring, "all") == 0) {
        session->all_chats = 1;
    } else if (cJSON_IsArray(chats)) {
        int n = 0;
        const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
        const cJSON *item;
        cJSON_ArrayForEach(item, chats) {
            if (!cJSON_IsString(item) || session->chat_count >= CONTROL_SESSION_CHATS) continue;
            int found = -1;
            if (automation_manager_resolve(s->deps.automation, all, n, item->valuestring, &found, NULL, 0, NULL) != CHAT_RESOLUTION_FOUND) {
                char why[256];
                snprintf(why, sizeof(why), "\"%s\" is not one chat you may see", item->valuestring);
                control_fail(s, session->conn, req->id, "not_found", why);
                return;
            }
            str_copy(session->chats[session->chat_count++], sizeof(session->chats[0]), all[found].jid);
        }
    } else {
        control_fail(s, session->conn, req->id, "bad_request", "\"chats\" is a list of chats, or \"all\"");
        return;
    }
    session->subscribed = 1;
    snapshot(s, session);
    control_reply(s, session->conn, control_codec_ok(req->id, NULL));
}

void control_op_unsubscribe(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    session->subscribed = 0;
    session->all_chats = 0;
    session->chat_count = 0;
    control_session_dispose(session);
    control_reply(s, session->conn, control_codec_ok(req->id, NULL));
}

static void send_message(ControlServer *s, const LiveMessageRef *ref) {
    const Chat *chat = find_chat(s, ref->chat_jid);
    if (!chat || !automation_manager_chat_allowed(s->deps.automation, chat)) return;
    Message msg;
    int loaded = 0;
    for (int i = 0; i < s->session_count; i++) {
        ControlSession *session = &s->sessions[i];
        if (!session->greeted || !control_session_follows(session, ref->chat_jid)) continue;
        if (!loaded) {
            if (messaging_manager_get(s->deps.messaging, ref->id, &msg) != 0) return;
            loaded = 1;
        }
        if (!automation_manager_pushes(s->deps.automation, session->origin, msg.from_me)) continue;
        char name[128];
        control_sender_name(s, &msg, name, sizeof(name));
        cJSON *evt = cJSON_CreateObject();
        cJSON *c = cJSON_AddObjectToObject(evt, "chat");
        cJSON_AddStringToObject(c, "jid", chat->jid);
        cJSON_AddStringToObject(c, "name", chat->name);
        cJSON_AddItemToObject(evt, "message", control_codec_message(&msg, name));
        control_tag_account(s, evt);
        control_reply(s, session->conn, control_codec_event("message", evt));
    }
    if (loaded) message_dispose(&msg);
}

static const char *event_name(LiveKind kind) {
    switch (kind) {
        case LIVE_KIND_READ:           return "read";
        case LIVE_KIND_REACTION:       return "reaction";
        case LIVE_KIND_EDIT:           return "edit";
        case LIVE_KIND_DELETE:         return "delete";
        case LIVE_KIND_SCHEDULED_SENT: return "scheduled_sent";
        default:                       return NULL;
    }
}

/* Something happened to a message: {"evt":name,"chat":{jid,name},"message_id","at"}, with
 * "who":{jid,name} where someone did it (as "reader" for a read), "emoji" for a reaction
 * ("" when taken back), and the message as it now reads for an edit. */
static void send_activity(ControlServer *s, const LiveMessageRef *ref) {
    const char *evt_name = event_name(ref->kind);
    const Chat *chat = find_chat(s, ref->chat_jid);
    if (!evt_name || !chat || !automation_manager_chat_allowed(s->deps.automation, chat)) return;
    char name[128] = "";
    if (ref->who[0]) messaging_manager_display_name(s->deps.messaging, ref->who, name, sizeof(name));
    for (int i = 0; i < s->session_count; i++) {
        ControlSession *session = &s->sessions[i];
        if (!session->greeted || !control_session_follows(session, ref->chat_jid)) continue;
        if (!automation_manager_pushes_event(s->deps.automation, session->origin, ref->kind)) continue;
        cJSON *evt = cJSON_CreateObject();
        cJSON *c = cJSON_AddObjectToObject(evt, "chat");
        cJSON_AddStringToObject(c, "jid", chat->jid);
        cJSON_AddStringToObject(c, "name", chat->name);
        cJSON_AddStringToObject(evt, "message_id", ref->id);
        if (ref->who[0]) {
            cJSON *who = cJSON_AddObjectToObject(evt, ref->kind == LIVE_KIND_READ ? "reader" : "who");
            cJSON_AddStringToObject(who, "jid", ref->who);
            cJSON_AddStringToObject(who, "name", name);
        }
        if (ref->kind == LIVE_KIND_REACTION) cJSON_AddStringToObject(evt, "emoji", ref->detail);
        if (ref->kind == LIVE_KIND_EDIT) {
            Message msg;
            if (messaging_manager_get(s->deps.messaging, ref->id, &msg) == 0) {
                char sender[128];
                control_sender_name(s, &msg, sender, sizeof(sender));
                cJSON_AddItemToObject(evt, "message", control_codec_message(&msg, sender));
                message_dispose(&msg);
            }
        }
        cJSON_AddNumberToObject(evt, "at", (double)ref->at);
        control_tag_account(s, evt);
        control_reply(s, session->conn, control_codec_event(evt_name, evt));
    }
}

static void send_unread_changes(ControlServer *s, ControlSession *session) {
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    for (int i = 0; i < n; i++) {
        if (!control_session_follows(session, all[i].jid)) continue;
        ControlWatch *w = NULL;
        for (int k = 0; k < session->watch_count && !w; k++) if (!strcmp(session->watches[k].jid, all[i].jid)) w = &session->watches[k];
        int allowed = automation_manager_chat_allowed(s->deps.automation, &all[i]);
        if (!allowed) continue;
        if (w && w->unread == all[i].unread) continue;
        if (!w && all[i].unread == 0) continue;
        cJSON *evt = cJSON_CreateObject();
        cJSON_AddItemToObject(evt, "chat", control_codec_chat(&all[i]));
        control_tag_account(s, evt);
        control_reply(s, session->conn, control_codec_event("chat", evt));
        if (!w) { snapshot(s, session); return; }         /* a chat it had not seen: start over */
        w->unread = all[i].unread;
    }
}

/* How far the live messages of the account being served were sent. Each
 * account counts its own, so each has its own place. */
static uint64_t *live_cursor(ControlServer *s) {
    if (s->account == ACCOUNT_ID_NONE) return &s->live_seq;
    int free_slot = -1;
    for (int i = 0; i < ACCOUNT_MAX; i++) {
        if (s->live_accounts[i] == s->account) return &s->live_seqs[i];
        if (free_slot < 0 && s->live_accounts[i] == ACCOUNT_ID_NONE) free_slot = i;
    }
    if (free_slot < 0) free_slot = 0;                        /* an account that is gone gives up its place */
    s->live_accounts[free_slot] = s->account;
    s->live_seqs[free_slot] = 0;
    return &s->live_seqs[free_slot];
}

/* What happened in the account being served since last time, to whoever follows it. */
static void push_live(ControlServer *s) {
    LiveMessageRef refs[LIVE_PER_TICK];
    uint64_t *cursor = live_cursor(s);
    int n = messaging_manager_live_since(s->deps.messaging, *cursor, refs, LIVE_PER_TICK);
    for (int i = 0; i < n; i++) {
        if (refs[i].kind == LIVE_KIND_MESSAGE) send_message(s, &refs[i]);
        else send_activity(s, &refs[i]);
        *cursor = refs[i].seq;
    }
}

void control_live_tick(ControlServer *s, int check_unread) {
    /* Every account agents may use is looked at in turn, each served while it is. */
    int accounts = control_account_count(s);
    for (int a = 0; a < accounts; a++) {
        if (control_serve_account(s, control_account_at(s, a)) != 0) continue;
        push_live(s);
    }
    if (!check_unread) return;
    /* Unread counts are watched for the default account, the one a client is on when it names none. */
    if (control_serve_account(s, control_default_account(s)) != 0) return;
    for (int i = 0; i < s->session_count; i++) {
        if (s->sessions[i].greeted && s->sessions[i].subscribed) send_unread_changes(s, &s->sessions[i]);
    }
}
