/* Who a new message is for. Most often a chat; when none matches, someone
 * who has no chat yet, so that a first message can be sent to them. */
#include "control_server_state.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

#define MAX_CONTACTS   32
#define MAX_CANDIDATES 8

static void answer_ambiguous(ControlServer *s, const ControlSession *session, const ControlRequest *req, const char *ref,
                             const Recipient *people, int count) {
    cJSON *extra = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(extra, "candidates");
    for (int i = 0; i < count; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "jid", people[i].jid);
        cJSON_AddStringToObject(c, "name", people[i].name);
        cJSON_AddBoolToObject(c, "new_chat", 1);
        control_tag_account(s, c);
        cJSON_AddItemToArray(list, c);
    }
    char why[256];
    snprintf(why, sizeof(why), "\"%s\" matches more than one contact", ref);
    control_reply(s, session->conn, control_codec_error(req->id, "ambiguous", why, extra));
}

int control_resolve_recipient(ControlServer *s, const ControlSession *session, const ControlRequest *req, const char *name,
                              char *jid, size_t size, int *new_chat) {
    *new_chat = 0;
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    const char *ref = control_codec_string(req->args, name);
    int at = -1, unused = 0;
    if (ref && *ref && automation_manager_resolve(s->deps.automation, all, n, ref, &at, NULL, 0, &unused) == CHAT_RESOLUTION_NOT_FOUND) {
        Contact contacts[MAX_CONTACTS];
        int known = messaging_manager_find_contacts(s->deps.messaging, ref, contacts, MAX_CONTACTS);
        Recipient who, options[MAX_CANDIDATES];
        int count = 0;
        ChatResolution r = automation_manager_resolve_recipient(s->deps.automation, all, n, contacts, known, ref,
                                                                &who, options, MAX_CANDIDATES, &count);
        if (r == CHAT_RESOLUTION_FOUND) {
            str_copy(jid, size, who.jid);
            *new_chat = 1;
            return 0;
        }
        if (r == CHAT_RESOLUTION_AMBIGUOUS) {
            answer_ambiguous(s, session, req, ref, options, count);
            return -1;
        }
        char why[256];
        snprintf(why, sizeof(why), "No chat or contact matches \"%s\". A phone number needs its country code, as in +27821234567", ref);
        control_fail(s, session->conn, req->id, "not_found", why);
        return -1;
    }
    at = control_resolve_chat(s, session, req, name, all, n);   /* found, or it answers why not */
    if (at < 0) return -1;
    str_copy(jid, size, all[at].jid);
    return 0;
}
