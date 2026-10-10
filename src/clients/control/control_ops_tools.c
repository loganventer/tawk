/* The tools for a long chat list, over the control socket: your labels on
 * chats, the chats put aside with a reminder, and the chats awaiting a
 * reply. All three are kept on this computer and nothing here reaches
 * WhatsApp. Reading them follows the chats a client may see; a label is a
 * managing write and a reminder a write you are asked about, since each
 * changes what your own chat list shows. */
#include "control_server_state.h"
#include "engines/label_name.h"
#include "engines/reminder_rule.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define AWAITING_MAX 256

static int resolve(ControlServer *s, ControlSession *session, const ControlRequest *req, Chat *out) {
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    int at = control_resolve_chat(s, session, req, "chat", all, n);
    if (at < 0) return -1;
    *out = all[at];
    return 0;
}

static int have(ControlServer *s, const ControlSession *session, const ControlRequest *req, int there, const char *what) {
    if (there) return 1;
    char why[96];
    snprintf(why, sizeof(why), "This tawk keeps no %s", what);
    control_fail(s, session->conn, req->id, "failed", why);
    return 0;
}

static cJSON *chat_ref(const Chat *chat) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "jid", chat->jid);
    cJSON_AddStringToObject(o, "name", chat->name);
    return o;
}

void control_tag_tools(ControlServer *s, cJSON *object, const Chat *chat) {
    if (!chat) return;
    if (s->deps.labels) {
        char list[160];
        label_manager_of(s->deps.labels, chat->jid, list, sizeof(list));
        cJSON *labels = cJSON_AddArrayToObject(object, "labels");
        for (char *p = list; *p;) {
            char *end = strstr(p, ", ");
            if (end) *end = '\0';
            cJSON_AddItemToArray(labels, cJSON_CreateString(p));
            if (!end) break;
            p = end + 2;
        }
    }
    int64_t due = 0;
    if (s->deps.reminders && reminder_manager_snoozed(s->deps.reminders, chat->jid, &due)) {
        cJSON *r = cJSON_AddObjectToObject(object, "reminder");
        cJSON_AddNumberToObject(r, "due_at", (double)due);
    }
}

/* ---- labels ---- */

/* {"labels":[...]}: every label in use, or with "chat" the labels of that chat. */
void control_op_list_labels(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!have(s, session, req, s->deps.labels != NULL, "labels")) return;
    cJSON *r = cJSON_CreateObject();
    if (control_codec_string(req->args, "chat")) {
        Chat chat;
        if (resolve(s, session, req, &chat) != 0) { cJSON_Delete(r); return; }
        cJSON_AddItemToObject(r, "chat", chat_ref(&chat));
        control_tag_tools(s, r, &chat);
        cJSON_DeleteItemFromObjectCaseSensitive(r, "reminder");
    } else {
        char all[LABELS_MAX][CHAT_LABEL_SIZE];
        int n = label_manager_all(s->deps.labels, all, LABELS_MAX);
        cJSON *labels = cJSON_AddArrayToObject(r, "labels");
        for (int i = 0; i < n; i++) cJSON_AddItemToArray(labels, cJSON_CreateString(all[i]));
    }
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

static cJSON *do_set_label(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    const char *label = control_codec_string(p->args, "label");
    int want = control_codec_bool(p->args, "on", 1);
    char clean[CHAT_LABEL_SIZE];
    int has = label && label_name_clean(label, clean, sizeof(clean)) == 0 && label_manager_has(s->deps.labels, p->chat_jid, clean);
    if (has != want && label_manager_toggle(s->deps.labels, p->chat_jid, label, clean) < 0) {
        str_copy(f->why, sizeof(f->why), "The label could not be kept");
        return NULL;
    }
    cJSON *r = cJSON_CreateObject();
    const Chat *chat = control_visible_chat(s, p->chat_jid);
    if (chat) control_tag_tools(s, r, chat);
    cJSON_DeleteItemFromObjectCaseSensitive(r, "reminder");
    return r;
}

void control_op_set_label(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!have(s, session, req, s->deps.labels != NULL, "labels")) return;
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    const char *label = control_required(s, session, req, "label");
    if (!label) return;
    char clean[CHAT_LABEL_SIZE];
    if (label_name_clean(label, clean, sizeof(clean)) != 0) {
        control_fail(s, session->conn, req->id, "bad_request", "A label is up to 24 characters, with no comma or slash");
        return;
    }
    int on = control_codec_bool(req->args, "on", 1);
    ControlPending p;
    control_pending_init(&p, req->id, "set_label", WRITE_KIND_MANAGE, do_set_label);
    str_copy(p.chat_jid, sizeof(p.chat_jid), chat.jid);
    cJSON_AddStringToObject(p.args, "label", clean);
    cJSON_AddBoolToObject(p.args, "on", on);
    snprintf(p.action, sizeof(p.action), on ? "label it \"%s\"" : "take the label \"%s\" off it", clean);
    control_write(s, session, &p);
}

/* ---- reminders ---- */

/* {"reminders":[{"chat":{jid,name},"due_at":N}]}; due_at 0 means until they write. */
void control_op_list_reminders(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!have(s, session, req, s->deps.reminders != NULL, "reminders")) return;
    cJSON *r = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(r, "reminders");
    int n = reminder_manager_count(s->deps.reminders);
    for (int i = 0; i < n; i++) {
        const ChatReminder *reminder = reminder_manager_at(s->deps.reminders, i);
        const Chat *chat = reminder ? control_visible_chat(s, reminder->jid) : NULL;
        if (!chat) continue;                                /* in another account, or one the client may not see */
        cJSON *o = cJSON_CreateObject();
        cJSON_AddItemToObject(o, "chat", chat_ref(chat));
        cJSON_AddNumberToObject(o, "due_at", (double)reminder->due_at);
        cJSON_AddItemToArray(list, o);
    }
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}

static cJSON *do_set_reminder(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    int64_t due = control_codec_int(p->args, "due_at", 0, 0, INT64_MAX);
    if (reminder_manager_set(s->deps.reminders, p->chat_jid, due, (int64_t)time(NULL)) != 0) {
        str_copy(f->why, sizeof(f->why), "The reminder could not be kept");
        return NULL;
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "due_at", (double)due);
    return r;
}

static cJSON *do_cancel_reminder(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    if (reminder_manager_clear(s->deps.reminders, p->chat_jid) != 0) {
        str_copy(f->why, sizeof(f->why), "The reminder could not be removed");
        return NULL;
    }
    return cJSON_CreateObject();
}

/* "when" is read as /remind reads it ("9:00", "tomorrow", "+2h", "reply"); "due_at" gives the time itself. */
void control_op_set_reminder(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!have(s, session, req, s->deps.reminders != NULL, "reminders")) return;
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    int64_t now = (int64_t)time(NULL), due = 0;
    const char *when = control_codec_string(req->args, "when");
    if (when && *when) {
        if (reminder_rule_parse(when, now, &due) != 0) {
            control_fail(s, session->conn, req->id, "bad_request", "\"when\" takes a time ahead (9:00, tomorrow, +2h, fri 17:30) or \"reply\"");
            return;
        }
    } else {
        due = control_codec_int(req->args, "due_at", -1, -1, INT64_MAX);
        if (due < 0 || (due > 0 && due <= now)) {
            control_fail(s, session->conn, req->id, "bad_request", "Give \"when\", or \"due_at\" as a time ahead in epoch seconds (0: until they write)");
            return;
        }
    }
    ControlPending p;
    control_pending_init(&p, req->id, "set_reminder", WRITE_KIND_SEND, do_set_reminder);
    str_copy(p.chat_jid, sizeof(p.chat_jid), chat.jid);
    cJSON_AddNumberToObject(p.args, "due_at", (double)due);
    if (due == 0) {
        snprintf(p.action, sizeof(p.action), "put it aside until they write");
    } else {
        char at[48];
        time_t t = (time_t)due;
        struct tm tm_due;
        localtime_r(&t, &tm_due);
        strftime(at, sizeof(at), "%a %d %b %H:%M", &tm_due);
        snprintf(p.action, sizeof(p.action), "put it aside until %s", at);
    }
    control_write(s, session, &p);
}

void control_op_cancel_reminder(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    if (!have(s, session, req, s->deps.reminders != NULL, "reminders")) return;
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    if (!reminder_manager_snoozed(s->deps.reminders, chat.jid, NULL)) {
        control_fail(s, session->conn, req->id, "not_found", "That chat is not put aside");
        return;
    }
    ControlPending p;
    control_pending_init(&p, req->id, "cancel_reminder", WRITE_KIND_SEND, do_cancel_reminder);
    str_copy(p.chat_jid, sizeof(p.chat_jid), chat.jid);
    snprintf(p.action, sizeof(p.action), "bring it back into the chat list");
    control_write(s, session, &p);
}

/* ---- awaiting a reply ---- */

/* {"days":N,"chats":[chat]}: the one-to-one chats where your last message is at least `days` days old and unanswered. */
void control_op_awaiting_replies(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    int days = (int)control_codec_int(req->args, "days", control_settings(s)->awaiting_days, 1, 60);
    static char jids[AWAITING_MAX][128];
    int n = messaging_manager_awaiting(s->deps.messaging, days, jids, AWAITING_MAX);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "days", days);
    cJSON *list = cJSON_AddArrayToObject(r, "chats");
    for (int i = 0; i < n; i++) {
        const Chat *chat = control_visible_chat(s, jids[i]);
        if (chat) cJSON_AddItemToArray(list, control_chat_json(s, session->origin, chat));
    }
    control_tag_account(s, r);
    control_reply(s, session->conn, control_codec_ok(req->id, r));
}
