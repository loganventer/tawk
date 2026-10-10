/* Reading over the control socket. Nothing here marks anything read,
 * sends a receipt or changes the chat open in the terminal. */
#include "control_server_state.h"
#include "engines/chat_match.h"
#include "utilities/str_util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_STATUS_AUTHORS 128
#define MAX_STATUSES_EACH  32

static const Chat *chats_of(ControlServer *s, int *count) { return messaging_manager_chats(s->deps.messaging, count); }

static cJSON *messages_json(ControlServer *s, const Message *items, int count) {
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        char name[128];
        control_sender_name(s, &items[i], name, sizeof(name));
        cJSON_AddItemToArray(list, control_codec_message(&items[i], name));
    }
    return list;
}

void control_op_list_chats(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const char *filter = control_codec_string(req->args, "filter");
    int unread_only = control_codec_bool(req->args, "unread_only", 0);
    int limit = (int)control_codec_int(req->args, "limit", 50, 1, 500);
    int n = 0;
    const Chat *all = chats_of(s, &n);
    cJSON *r = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(r, "chats");
    int added = 0;
    for (int i = 0; i < n && added < limit; i++) {
        if (!automation_manager_chat_allowed(s->deps.automation, &all[i])) continue;
        if (!chat_match_filter(&all[i], filter)) continue;
        if (unread_only && all[i].unread == 0) continue;
        cJSON_AddItemToArray(list, control_codec_chat(&all[i]));
        added++;
    }
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

void control_op_read_messages(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    int n = 0;
    const Chat *all = chats_of(s, &n);
    int at = control_resolve_chat(s, session, req, "chat", all, n);
    if (at < 0) return;
    Chat chat = all[at];
    int64_t before = control_codec_int(req->args, "before", 0, 0, INT64_MAX);
    int limit = (int)control_codec_int(req->args, "limit", 30, 1, 200);
    Message *items = NULL;
    int count = 0;
    if (messaging_manager_history(s->deps.messaging, chat.jid, before, limit, &items, &count) != 0) {
        control_fail(s, session->conn, req->id, "failed", "The messages could not be read");
        return;
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "chat", control_codec_chat(&chat));
    cJSON_AddItemToObject(r, "messages", messages_json(s, items, count));
    cJSON_AddNumberToObject(r, "next_before", count == limit && count > 0 ? (double)items[0].timestamp : 0);
    message_array_free(items, count);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

void control_op_search_messages(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const char *query = control_codec_string(req->args, "query");
    if (!query || !*query) { control_fail(s, session->conn, req->id, "bad_request", "\"query\" is required"); return; }
    int limit = (int)control_codec_int(req->args, "limit", 20, 1, 100);
    const char *only = NULL;
    if (control_codec_string(req->args, "chat")) {
        int n = 0;
        const Chat *all = chats_of(s, &n);
        int at = control_resolve_chat(s, session, req, "chat", all, n);
        if (at < 0) return;
        only = all[at].jid;
    }
    char only_jid[128] = "";
    if (only) str_copy(only_jid, sizeof(only_jid), only);
    Message *items = NULL;
    int count = 0;
    /* Ask for more than needed: hits in chats the client may not see are dropped. */
    if (messaging_manager_search(s->deps.messaging, query, limit * 4, &items, &count) != 0) {
        control_fail(s, session->conn, req->id, "failed", "Search failed");
        return;
    }
    int kept = 0;
    for (int i = 0; i < count; i++) {
        int keep = kept < limit && control_visible_chat(s, items[i].chat_jid) && (!only_jid[0] || strcmp(items[i].chat_jid, only_jid) == 0);
        if (!keep) { message_dispose(&items[i]); continue; }
        if (kept != i) items[kept] = items[i];
        kept++;
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "messages", messages_json(s, items, kept));
    message_array_free(items, kept);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

void control_op_unread_summary(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    int n = 0;
    const Chat *all = chats_of(s, &n);
    int total = 0, mentions = 0;
    cJSON *r = cJSON_CreateObject();
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (all[i].unread == 0 || !automation_manager_chat_allowed(s->deps.automation, &all[i])) continue;
        total += all[i].unread > 0 ? all[i].unread : 1;          /* -1: marked unread by hand */
        mentions += all[i].unread_mention ? 1 : 0;
        cJSON_AddItemToArray(list, control_codec_chat(&all[i]));
    }
    cJSON_AddNumberToObject(r, "total", total);
    cJSON_AddNumberToObject(r, "mentions", mentions);
    cJSON_AddItemToObject(r, "chats", list);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

/* "jid\t1\n" per member, 1 for an admin. */
static void add_members(ControlServer *s, cJSON *r, const char *participants) {
    cJSON *list = cJSON_AddArrayToObject(r, "members");
    const char *p = participants;
    while (p && *p) {
        const char *end = strchr(p, '\n');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        char line[200];
        str_copy(line, sizeof(line) < len + 1 ? sizeof(line) : len + 1, p);
        char *tab = strchr(line, '\t');
        int admin = tab && tab[1] == '1';
        if (tab) *tab = '\0';
        if (line[0]) {
            char name[128];
            messaging_manager_display_name(s->deps.messaging, line, name, sizeof(name));
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "jid", line);
            cJSON_AddStringToObject(m, "name", name);
            cJSON_AddBoolToObject(m, "admin", admin);
            cJSON_AddItemToArray(list, m);
        }
        p = end ? end + 1 : NULL;
    }
}

void control_op_chat_info(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    int n = 0;
    const Chat *all = chats_of(s, &n);
    int at = control_resolve_chat(s, session, req, "chat", all, n);
    if (at < 0) return;
    Chat chat = all[at];
    cJSON *r = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "chat", control_codec_chat(&chat));
    control_tag_transcribe(s, r, &chat);
    control_tag_tldr(s, r, &chat);
    ContactProfile profile;
    if (s->deps.profiles && profile_manager_details(s->deps.profiles, chat.jid, 0, &profile) == 0) {
        cJSON_AddStringToObject(r, "about", chat.is_group ? profile.group_description : profile.about);
        if (chat.is_group) add_members(s, r, profile.participants);
        contact_profile_dispose(&profile);
    } else {
        cJSON_AddStringToObject(r, "about", "");
    }
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

/* Your own statuses always; other people's when their chat is one the client may see,
 * or, for someone you have no chat with, when no chat list narrows what it sees. */
static int status_visible(ControlServer *s, const StatusAuthor *a) {
    if (a->from_me) return 1;
    int n = 0;
    const Chat *all = chats_of(s, &n);
    for (int i = 0; i < n; i++) {
        if (strcmp(all[i].jid, a->jid) == 0) return automation_manager_chat_allowed(s->deps.automation, &all[i]);
    }
    Chat stub;
    chat_init(&stub, a->jid);
    str_copy(stub.name, sizeof(stub.name), a->name);
    stub.is_locked = 0;
    return automation_manager_chat_allowed(s->deps.automation, &stub);
}

void control_op_list_statuses(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    int archived = control_codec_bool(req->args, "include_archived", 0);
    cJSON *r = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(r, "statuses");
    for (int pass = 0; pass <= archived && s->deps.feed; pass++) {
        StatusAuthor authors[MAX_STATUS_AUTHORS];
        int n = status_feed_manager_authors(s->deps.feed, pass, authors, MAX_STATUS_AUTHORS);
        for (int a = 0; a < n; a++) {
            if (!status_visible(s, &authors[a])) continue;
            StatusUpdate items[MAX_STATUSES_EACH];
            int count = status_feed_manager_updates(s->deps.feed, authors[a].jid, pass, items, MAX_STATUSES_EACH);
            char name[128];
            if (authors[a].from_me) str_copy(name, sizeof(name), "You");
            else messaging_manager_display_name(s->deps.messaging, authors[a].jid, name, sizeof(name));
            for (int i = 0; i < count; i++) {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "id", items[i].id);
                cJSON_AddStringToObject(o, "author", items[i].author_jid);
                cJSON_AddStringToObject(o, "author_name", name);
                cJSON_AddBoolToObject(o, "from_me", items[i].from_me);
                cJSON_AddStringToObject(o, "type", message_type_name(items[i].type));
                if (items[i].text) cJSON_AddStringToObject(o, "text", items[i].text);
                cJSON_AddNumberToObject(o, "ts", (double)items[i].timestamp);
                cJSON_AddBoolToObject(o, "viewed", items[i].viewed);
                cJSON_AddItemToArray(list, o);
            }
            for (int i = 0; i < count; i++) status_update_dispose(&items[i]);
        }
    }
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

void control_op_list_scheduled(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const char *only = NULL;
    int n = 0;
    const Chat *all = chats_of(s, &n);
    if (control_codec_string(req->args, "chat")) {
        int at = control_resolve_chat(s, session, req, "chat", all, n);
        if (at < 0) return;
        only = all[at].jid;
    }
    ScheduledMessage *items = NULL;
    int count = 0;
    scheduling_manager_list(s->deps.scheduling, only, &items, &count);
    cJSON *r = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(r, "scheduled");
    for (int i = 0; i < count; i++) {
        if (!control_visible_chat(s, items[i].chat_jid)) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", items[i].id);
        cJSON_AddStringToObject(o, "chat", items[i].chat_jid);
        cJSON_AddStringToObject(o, "text", items[i].text ? items[i].text : "");
        cJSON_AddNumberToObject(o, "due_at", (double)items[i].due_at);
        cJSON_AddItemToArray(list, o);
    }
    scheduled_message_array_free(items, count);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}
