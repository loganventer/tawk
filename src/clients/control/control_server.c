#include "control_server_state.h"
#include "clients/control/control_op_entry.h"
#include "utilities/app_info.h"
#include "utilities/clock_util.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define INBOUND_PER_TICK 64
#define RETRY_LISTEN_MS  5000
#define CHECK_MS         500

static const ControlOpEntry OPS[] = {
    { "list_chats",             control_op_list_chats, 1 },
    { "read_messages",          control_op_read_messages, 1 },
    { "search_messages",        control_op_search_messages, 1 },
    { "unread_summary",         control_op_unread_summary, 1 },
    { "chat_info",              control_op_chat_info, 1 },
    { "presence",               control_op_presence, 1 },
    { "list_statuses",          control_op_list_statuses, 1 },
    { "list_scheduled",         control_op_list_scheduled, 1 },
    { "send_message",           control_op_send_message, 0 },
    { "react",                  control_op_react, 0 },
    { "schedule_message",       control_op_schedule_message, 0 },
    { "mark_read",              control_op_mark_read, 0 },
    { "draft_message",          control_op_draft_message, 0 },
    { "edit_message",           control_op_edit_message, 0 },
    { "delete_message",         control_op_delete_message, 0 },
    { "forward_message",        control_op_forward_message, 0 },
    { "retry_message",          control_op_retry_message, 0 },
    { "download_media",         control_op_download_media, 1 },
    { "get_transcript",         control_op_get_transcript, 1 },
    { "set_transcript",         control_op_set_transcript, 0 },
    { "get_summary",            control_op_get_summary, 1 },
    { "set_summary",            control_op_set_summary, 0 },
    { "set_chat",               control_op_set_chat, 0 },
    { "set_chat_theme",         control_op_set_chat_theme, 0 },
    { "clear_chat",             control_op_clear_chat, 0 },
    { "delete_chat",            control_op_delete_chat, 0 },
    { "export_chat",            control_op_export_chat, 0 },
    { "block",                  control_op_block, 0 },
    { "unblock",                control_op_unblock, 0 },
    { "cancel_scheduled",       control_op_cancel_scheduled, 0 },
    { "reschedule",             control_op_reschedule, 0 },
    { "send_scheduled_now",     control_op_send_scheduled_now, 0 },
    { "status_viewers",         control_op_status_viewers, 1 },
    { "list_backgrounds",       control_op_list_backgrounds, 1 },
    { "post_status",            control_op_post_status, 0 },
    { "reply_status",           control_op_reply_status, 0 },
    { "like_status",            control_op_like_status, 0 },
    { "get_profile",            control_op_get_profile, 1 },
    { "set_profile",            control_op_set_profile, 0 },
    { "set_profile_photo",      control_op_set_profile_photo, 0 },
    { "remove_profile_photo",   control_op_remove_profile_photo, 0 },
    { "get_settings",           control_op_get_settings, 1 },
    { "set_setting",            control_op_set_setting, 0 },
    { "list_themes",            control_op_list_themes, 1 },
    { "app_status",             control_op_app_status, 1 },
    { "describe",               control_op_describe, 1 },
    { "list_accounts",          control_op_list_accounts, 1 },
    { "reconnect",              control_op_reconnect, 0 },
    { "decline_call",           control_op_decline_call, 0 },
    { "confirm",                control_op_confirm, 0 },
    { "cancel_confirmation",    control_op_cancel_confirmation, 0 },
    { "approve",                control_op_approve, 0 },
    { "subscribe",              control_op_subscribe, 1 },
    { "unsubscribe",            control_op_unsubscribe, 0 },
};

/* ---- shared helpers ----------------------------------------------------- */

const Settings *control_settings(ControlServer *s) { return settings_manager_current(s->deps.settings); }

ControlSession *control_session_of(ControlServer *s, int conn) {
    for (int i = 0; i < s->session_count; i++) if (s->sessions[i].conn == conn) return &s->sessions[i];
    return NULL;
}

void control_reply(ControlServer *s, int conn, char *line) {
    if (line) s->deps.transport->send(s->deps.transport, conn, line);
    free(line);
}

void control_fail(ControlServer *s, int conn, const char *id, const char *code, const char *message) {
    control_reply(s, conn, control_codec_error(id, code, message, NULL));
}

int control_resolve_chat(ControlServer *s, const ControlSession *session, const ControlRequest *req, const char *name,
                         const Chat *chats, int count) {
    const char *ref = control_codec_string(req->args, name);
    if (!ref || !*ref) {
        char why[96];
        snprintf(why, sizeof(why), "\"%s\" is required", name);
        control_fail(s, session->conn, req->id, "bad_request", why);
        return -1;
    }
    int found = -1, candidates[8], n = 0;
    ChatResolution r = automation_manager_resolve(s->deps.automation, chats, count, ref, &found, candidates, 8, &n);
    if (r == CHAT_RESOLUTION_FOUND) return found;
    char why[256];
    if (r == CHAT_RESOLUTION_AMBIGUOUS) {
        cJSON *extra = cJSON_CreateObject();
        cJSON *list = cJSON_AddArrayToObject(extra, "candidates");
        for (int i = 0; i < n; i++) {
            cJSON *c = cJSON_CreateObject();
            cJSON_AddStringToObject(c, "jid", chats[candidates[i]].jid);
            cJSON_AddStringToObject(c, "name", chats[candidates[i]].name);
            control_tag_account(s, c);
            cJSON_AddItemToArray(list, c);
        }
        snprintf(why, sizeof(why), "\"%s\" matches more than one chat", ref);
        control_reply(s, session->conn, control_codec_error(req->id, "ambiguous", why, extra));
    } else {
        snprintf(why, sizeof(why), "No chat matches \"%s\"", ref);
        control_fail(s, session->conn, req->id, "not_found", why);
    }
    return -1;
}

const Chat *control_visible_chat(ControlServer *s, const char *jid) {
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    for (int i = 0; i < n; i++) {
        if (strcmp(all[i].jid, jid) == 0) return automation_manager_chat_allowed(s->deps.automation, &all[i]) ? &all[i] : NULL;
    }
    return NULL;
}

const char *control_required(ControlServer *s, const ControlSession *session, const ControlRequest *req, const char *name) {
    const char *v = control_codec_string(req->args, name);
    if (v && *v) return v;
    char why[96];
    snprintf(why, sizeof(why), "\"%s\" is required", name);
    control_fail(s, session->conn, req->id, "bad_request", why);
    return NULL;
}

int control_load_message(ControlServer *s, const ControlSession *session, const ControlRequest *req, const char *name, Message *out) {
    const char *id = control_required(s, session, req, name);
    if (!id) return -1;
    if (messaging_manager_get(s->deps.messaging, id, out) != 0) {
        control_fail(s, session->conn, req->id, "not_found", "No such message");
        return -1;
    }
    if (!control_visible_chat(s, out->chat_jid)) {
        message_dispose(out);
        control_fail(s, session->conn, req->id, "not_found", "No such message");
        return -1;
    }
    return 0;
}

int control_connected(ControlServer *s) {
    return messaging_manager_auth_state(s->deps.messaging) == AUTH_STATE_CONNECTED;
}

void control_sender_name(ControlServer *s, const Message *m, char *out, unsigned long size) {
    if (m->from_me) { str_copy(out, size, "You"); return; }
    messaging_manager_display_name(s->deps.messaging, m->sender_jid[0] ? m->sender_jid : m->chat_jid, out, size);
    if (!out[0] && m->sender_name[0]) str_copy(out, size, m->sender_name);
}

/* ---- the handshake ------------------------------------------------------ */

static void hello(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const cJSON *protocol = cJSON_GetObjectItemCaseSensitive(req->args, "protocol");
    if (protocol && (!cJSON_IsNumber(protocol) || protocol->valueint != CONTROL_PROTOCOL)) {
        control_fail(s, session->conn, req->id, "protocol", "This tawk speaks control protocol 1");
        return;
    }
    const char *origin = control_codec_string(req->args, "origin");
    ControlOrigin parsed = CONTROL_ORIGIN_MCP;
    if (origin && control_origin_parse(origin, &parsed) != 0) {
        control_fail(s, session->conn, req->id, "bad_request", "\"origin\" is mcp or cli");
        return;
    }
    const char *client = control_codec_string(req->args, "client");
    session->origin = parsed;
    str_copy(session->client, sizeof(session->client), client && *client ? client : control_origin_name(parsed));
    str_strip_controls(session->client);
    /* Several copies of one program may connect, one for each session of an agent: a label tells them apart. */
    const char *label = control_codec_string(req->args, "label");
    str_copy(session->label, sizeof(session->label), label ? label : "");
    str_strip_controls(session->label);
    session->greeted = 1;
    s->changed = 1;
    if (parsed == CONTROL_ORIGIN_MCP) {
        automation_manager_record(s->deps.automation, parsed, session->client, "hello", "", "", AUTOMATION_OUTCOME_CONNECTED);
    }

    /* With no account open to agents the client is still greeted, and told so by an empty list. */
    int nobody = control_serve_account(s, control_default_account(s)) != 0;
    MessagingManager *mm = s->deps.messaging;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "protocol", CONTROL_PROTOCOL);
    cJSON_AddStringToObject(r, "tawk", APP_VERSION);
    cJSON_AddStringToObject(r, "access", automation_manager_access(s->deps.automation));
    cJSON *account = cJSON_AddObjectToObject(r, "account");
    cJSON_AddStringToObject(account, "jid", nobody ? "" : messaging_manager_user_jid(mm));
    cJSON_AddStringToObject(account, "name", nobody ? "" : messaging_manager_user_name(mm));
    cJSON_AddBoolToObject(r, "connected", !nobody && messaging_manager_auth_state(mm) == AUTH_STATE_CONNECTED);
    /* What this tawk can do beyond the first protocol, by name, so a client offers only what will work. */
    cJSON *features = cJSON_AddArrayToObject(r, "features");
    if (s->deps.transcripts) cJSON_AddItemToArray(features, cJSON_CreateString("transcripts"));
    if (s->deps.summaries) cJSON_AddItemToArray(features, cJSON_CreateString("summaries"));
    if (s->deps.directory && s->deps.roster) {              /* this tawk serves each request from the account it names */
        cJSON_AddBoolToObject(r, "multi_account", 1);
        cJSON_AddItemToObject(r, "accounts", control_accounts_json(s));
        cJSON_AddNumberToObject(r, "default_account", control_default_account(s));
    }
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

static void handle_line(ControlServer *s, int conn, const char *line) {
    ControlSession *session = control_session_of(s, conn);
    if (!session) return;
    ControlRequest req;
    if (control_codec_parse(line, &req) != 0) {
        control_fail(s, conn, req.id, "bad_request", "Each line must be a JSON object with a string \"id\" and \"op\"");
        control_request_dispose(&req);
        return;
    }
    if (strcmp(req.op, "hello") == 0) {
        hello(s, session, &req);
    } else if (!session->greeted) {
        control_fail(s, conn, req.id, "hello_first", "Send hello first");
    } else {
        const ControlOpEntry *entry = NULL;
        for (size_t i = 0; i < sizeof(OPS) / sizeof(OPS[0]) && !entry; i++) {
            if (strcmp(OPS[i].name, req.op) == 0) entry = &OPS[i];
        }
        if (entry && control_request_account(s, session, &req) != 0) {
            /* answered: it named an account it may not use */
        } else if (entry) {
            session->requests++;
            if (entry->read) {
                const char *chat = control_codec_string(req.args, "chat");
                automation_manager_record(s->deps.automation, session->origin, session->client, req.op,
                                          chat ? chat : "", "", AUTOMATION_OUTCOME_READ);
            }
            entry->run(s, session, &req);
        } else {
            control_fail(s, conn, req.id, "bad_request", "Unknown operation");
        }
    }
    control_request_dispose(&req);
}

/* ---- connections -------------------------------------------------------- */

static void opened(ControlServer *s, int conn) {
    if (s->session_count >= CONTROL_MAX_SESSIONS) {
        s->deps.transport->close_conn(s->deps.transport, conn);
        return;
    }
    control_session_init(&s->sessions[s->session_count++], conn);
}

static void closed(ControlServer *s, int conn) {
    control_writes_forget(s, conn);
    for (int i = 0; i < s->session_count; i++) {
        if (s->sessions[i].conn != conn) continue;
        control_session_dispose(&s->sessions[i]);
        s->sessions[i] = s->sessions[--s->session_count];
        s->changed = 1;
        return;
    }
}

static void say_goodbye(ControlServer *s) {
    for (int i = 0; i < s->session_count; i++) {
        if (s->sessions[i].greeted) control_reply(s, s->sessions[i].conn, control_codec_event("bye", NULL));
    }
    while (s->session_count > 0) closed(s, s->sessions[0].conn);
}

/* Listens while the setting is on, and again when the socket file is
 * removed from under it (a cleaner emptying the runtime folder). */
static void follow_setting(ControlServer *s, int64_t now) {
    int want = control_settings(s)->control_socket;
    if (!want) {
        if (s->listening) {
            say_goodbye(s);
            s->deps.transport->stop(s->deps.transport);
            s->listening = 0;
            s->changed = 1;
            LOG_INFO("control socket closed");
        }
        s->error[0] = '\0';
        s->next_listen_ms = 0;
        return;
    }
    struct stat st;
    if (s->listening && now >= s->next_check_ms && lstat(s->deps.socket_path, &st) != 0) {
        LOG_WARN("control socket %s disappeared; listening again", s->deps.socket_path);
        say_goodbye(s);
        s->listening = 0;
        s->next_listen_ms = 0;
    }
    if (s->listening || now < s->next_listen_ms) return;
    s->error[0] = '\0';
    if (s->deps.transport->listen(s->deps.transport, s->deps.socket_path, s->error, sizeof(s->error)) == 0) {
        s->listening = 1;
        s->live_seq = messaging_manager_live_last(s->deps.messaging);
        memset(s->live_accounts, 0, sizeof(s->live_accounts));   /* each account starts from now again */
    } else {
        LOG_WARN("control socket: %s", s->error);
        s->next_listen_ms = now + RETRY_LISTEN_MS;
    }
    s->changed = 1;
}

static void report_status(ControlServer *s) {
    AutomationStatus st;
    memset(&st, 0, sizeof(st));
    st.listening = s->listening;
    for (int i = 0; i < s->session_count; i++) {
        const ControlSession *c = &s->sessions[i];
        if (!c->greeted) continue;
        if (c->origin == CONTROL_ORIGIN_MCP) st.mcp_sessions++;
        else st.cli_sessions++;
        if (st.session_count >= AUTOMATION_STATUS_SESSIONS) continue;
        AutomationSession *out = &st.sessions[st.session_count++];
        out->conn = c->conn;
        str_copy(out->client, sizeof(out->client), c->client);
        str_copy(out->label, sizeof(out->label), c->label);
        str_copy(out->doing, sizeof(out->doing), c->doing);
        out->origin = c->origin;
        out->since = c->since;
        out->requests = c->requests;
        out->allowances = c->allowance_count;
        out->paused = c->paused;
        out->summariser = control_summariser_is(s, c);
    }
    for (int i = 0; i < s->pending_count; i++) if (s->pending[i].approval_id) st.waiting++;
    str_copy(st.socket_path, sizeof(st.socket_path), s->deps.socket_path);
    str_copy(st.error, sizeof(st.error), s->error);
    automation_manager_set_status(s->deps.automation, &st);
}

/* What you asked for in the Agents tab. */
static void obey(ControlServer *s) {
    AutomationCommand c;
    while (automation_manager_take_command(s->deps.automation, &c)) {
        ControlSession *session = control_session_of(s, c.conn);
        if (!session) continue;
        switch (c.kind) {
            case AUTOMATION_COMMAND_DISCONNECT:
                control_reply(s, c.conn, control_codec_event("bye", NULL));
                s->deps.transport->close_conn(s->deps.transport, c.conn);
                closed(s, c.conn);
                break;
            case AUTOMATION_COMMAND_REVOKE: session->allowance_count = 0; break;
            case AUTOMATION_COMMAND_PAUSE:  session->paused = 1; break;
            case AUTOMATION_COMMAND_RESUME: session->paused = 0; break;
            case AUTOMATION_COMMAND_SUMMARISER: control_summariser_choose(s, c.conn); break;
        }
        s->changed = 1;
    }
}

int control_server_tick(ControlServer *s) {
    int64_t now = clock_now_ms();
    char frame_context[64];
    log_context_get(frame_context, sizeof(frame_context));  /* serving an account marks the log with it; put back at the end */
    s->changed = 0;
    follow_setting(s, now);
    automation_manager_admin_wanted(s->deps.automation, control_any_admin(s));
    if (s->listening) automation_manager_tick(s->deps.automation);   /* the admin token follows the access levels */
    obey(s);
    if (s->listening) {
        ControlInbound in[INBOUND_PER_TICK];
        int n = s->deps.transport->poll(s->deps.transport, in, INBOUND_PER_TICK);
        for (int i = 0; i < n; i++) {
            if (in[i].kind == CONTROL_INBOUND_OPENED) opened(s, in[i].conn);
            else if (in[i].kind == CONTROL_INBOUND_LINE) handle_line(s, in[i].conn, in[i].line);
            else closed(s, in[i].conn);
            control_inbound_dispose(&in[i]);
        }
        int check = now >= s->next_check_ms;
        if (check) s->next_check_ms = now + CHECK_MS;
        control_live_tick(s, check);
        control_summaries_tick(s, now);
        control_transcripts_tick(s);
    }
    control_writes_tick(s, now);
    report_status(s);
    log_context_set(frame_context);
    return s->changed;
}

static int hook_tick(IFrameHook *self) { return control_server_tick(self->ctx); }

IFrameHook *control_server_frame_hook(ControlServer *s) { return &s->hook; }

ControlServer *control_server_create(const ControlServerDeps *deps) {
    ControlServer *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->deps = *deps;
    s->hook = (IFrameHook){ s, hook_tick };
    return s;
}

void control_server_destroy(ControlServer *s) {
    if (!s) return;
    say_goodbye(s);
    for (int i = 0; i < s->pending_count; i++) control_pending_dispose(&s->pending[i]);
    for (int i = 0; i < s->confirmation_count; i++) control_pending_dispose(&s->confirmations[i].pending);
    if (s->listening) s->deps.transport->stop(s->deps.transport);
    free(s);
}
