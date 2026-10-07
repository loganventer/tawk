#include "resource_access/json_protocol.h"
#include "utilities/base64.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define MAX_TEXT_BYTES (64 * 1024)
#define MAX_QR_BYTES   (32 * 1024)
#define MAX_REF_BYTES  (16 * 1024)
#define MAX_THUMB_BYTES (64 * 1024)

static const char *get_str(const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static double get_num(const cJSON *obj, const char *key, double fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

static int get_bool(const cJSON *obj, const char *key) {
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key));
}

/* Copies a single-line field, removing control characters. */
static void copy_line(char *dst, size_t size, const char *src) {
    str_copy(dst, size, src);
    str_strip_controls(dst);
}

/* Duplicates multi-line text up to a limit, keeping newlines and tabs only. */
static char *dup_text(const char *src, size_t limit) {
    size_t len = strnlen(src, limit);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        out[i] = (c < 0x20 && c != '\n' && c != '\t') || c == 0x7f ? ' ' : (char)c;
    }
    out[len] = '\0';
    return out;
}

#define MAX_MEMBERS_BYTES (256 * 1024)

/* A contact's or group's details. */
static int decode_profile(const cJSON *j, Event *e) {
    ContactProfile *p = calloc(1, sizeof(*p));
    if (!p) return 0;
    contact_profile_init(p, get_str(j, "jid"));
    copy_line(p->about, sizeof(p->about), get_str(j, "about"));
    copy_line(p->verified_name, sizeof(p->verified_name), get_str(j, "verified_name"));
    const cJSON *biz = cJSON_GetObjectItemCaseSensitive(j, "business");
    if (cJSON_IsObject(biz)) {
        p->is_business = 1;
        copy_line(p->business_category, sizeof(p->business_category), get_str(biz, "category"));
        copy_line(p->business_address, sizeof(p->business_address), get_str(biz, "address"));
        copy_line(p->business_email, sizeof(p->business_email), get_str(biz, "email"));
    }
    const cJSON *group = cJSON_GetObjectItemCaseSensitive(j, "group");
    if (cJSON_IsObject(group)) {
        p->is_group = 1;
        copy_line(p->group_subject, sizeof(p->group_subject), get_str(group, "subject"));
        char *desc = dup_text(get_str(group, "description"), sizeof(p->group_description) - 1);
        if (desc) { str_copy(p->group_description, sizeof(p->group_description), desc); free(desc); }
        copy_line(p->group_owner, sizeof(p->group_owner), get_str(group, "owner"));
        p->group_created = (int64_t)get_num(group, "created", 0);
        const cJSON *members = cJSON_GetObjectItemCaseSensitive(group, "participants");
        size_t cap = 0, used = 0;
        const cJSON *m;
        cJSON_ArrayForEach(m, members) {
            char jid[128];
            copy_line(jid, sizeof(jid), get_str(m, "jid"));
            if (!jid[0] || strchr(jid, '\t')) continue;
            size_t need = strlen(jid) + 4;
            if (used + need + 1 > cap) {
                size_t grown = cap ? cap * 2 : 4096;
                while (grown < used + need + 1) grown *= 2;
                if (grown > MAX_MEMBERS_BYTES) break;
                char *bigger = realloc(p->participants, grown);
                if (!bigger) break;
                p->participants = bigger;
                cap = grown;
            }
            used += (size_t)snprintf(p->participants + used, cap - used, "%s\t%d\n", jid, get_bool(m, "admin"));
            p->participant_count++;
        }
    }
    e->profile = p;
    return p->jid[0] != '\0';
}

/* The block list: JIDs, one per line. */
static int decode_blocklist(const cJSON *j, Event *e) {
    const cJSON *jids = cJSON_GetObjectItemCaseSensitive(j, "jids");
    size_t cap = 1024, used = 0;
    char *list = malloc(cap);
    if (!list) return 0;
    list[0] = '\0';
    const cJSON *item;
    cJSON_ArrayForEach(item, jids) {
        if (!cJSON_IsString(item)) continue;
        char jid[128];
        copy_line(jid, sizeof(jid), item->valuestring);
        size_t need = strlen(jid) + 2;
        if (used + need >= cap) {
            if (cap > MAX_MEMBERS_BYTES) break;
            char *bigger = realloc(list, cap * 2);
            if (!bigger) break;
            list = bigger;
            cap *= 2;
        }
        used += (size_t)snprintf(list + used, cap - used, "%s\n", jid);
    }
    e->list = list;
    return 1;
}

/* "mentions": [{"jid","user"}], the people a message mentions. Only
 * well-formed JIDs and digit-only user parts are kept. */
static void decode_mentions(const cJSON *j, Message *m) {
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(j, "mentions");
    if (!cJSON_IsArray(list)) return;
    MentionList mentions;
    mention_list_init(&mentions);
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        const char *jid = get_str(item, "jid"), *user = get_str(item, "user");
        if (!strchr(jid, '@') || strlen(jid) >= sizeof(mentions.items[0].jid)) continue;
        int digits = user[0] != '\0';
        for (const char *p = user; *p; p++) digits &= *p >= '0' && *p <= '9';
        if (digits) mention_list_add(&mentions, jid, user);
    }
    char *text = mention_list_serialize(&mentions);
    message_set_mentions(m, text);
    free(text);
}

static void decode_link(const cJSON *j, Message *m) {
    const cJSON *link = cJSON_GetObjectItemCaseSensitive(j, "link");
    if (!cJSON_IsObject(link)) return;
    const char *url = get_str(link, "url");
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) return;
    char title[256], desc[512];
    copy_line(title, sizeof(title), get_str(link, "title"));
    copy_line(desc, sizeof(desc), get_str(link, "desc"));
    message_set_link(m, link_preview_create(url, title, desc));
}

static void decode_message(const cJSON *j, Event *e) {
    Message *m = &e->message;
    copy_line(m->id, sizeof(m->id), get_str(j, "id"));
    copy_line(m->chat_jid, sizeof(m->chat_jid), get_str(j, "chat"));
    copy_line(m->sender_jid, sizeof(m->sender_jid), get_str(j, "sender"));
    copy_line(m->sender_name, sizeof(m->sender_name), get_str(j, "sender_name"));
    const char *text = get_str(j, "text");
    if (text[0]) m->text = dup_text(text, MAX_TEXT_BYTES);
    const char *ref = get_str(j, "ref");
    if (ref[0] && strlen(ref) < MAX_REF_BYTES) m->media_ref = str_dup(ref);
    m->type = message_type_parse(get_str(j, "type"));
    m->status = message_status_parse(get_str(j, "status"));
    m->timestamp = (int64_t)get_num(j, "ts", 0);
    m->from_me = get_bool(j, "from_me");
    m->duration_s = (int)get_num(j, "seconds", 0);
    double bg = get_num(j, "bg", 0);
    if (bg > 0 && bg <= 4294967295.0) m->background_argb = (uint32_t)bg;
    /* Only your own statuses come with a local file; whoever keeps them
     * checks the path is inside the media folder before trusting it. */
    if (!strcmp(m->chat_jid, "status@broadcast")) copy_line(m->media_path, sizeof(m->media_path), get_str(j, "path"));
    e->live = get_bool(j, "live");
    decode_mentions(j, m);
    m->mentions_me = get_bool(j, "mentions_me");
    m->forwarded = get_bool(j, "forwarded");
    decode_link(j, m);
    const cJSON *quote = cJSON_GetObjectItemCaseSensitive(j, "quote");
    if (cJSON_IsObject(quote)) {
        copy_line(m->quoted_id, sizeof(m->quoted_id), get_str(quote, "id"));
        copy_line(m->quoted_sender, sizeof(m->quoted_sender), get_str(quote, "sender"));
        m->quoted_status = get_bool(quote, "status");
        char *qt = dup_text(get_str(quote, "text"), 512);
        if (qt) { for (char *p = qt; *p; p++) if (*p == '\n' || *p == '\t') *p = ' '; }
        message_set_quoted_text(m, qt);
        free(qt);
    }
    const char *thumb = get_str(j, "thumb");
    if (thumb[0]) {
        int len = 0;
        unsigned char *bytes = base64_decode(thumb, MAX_THUMB_BYTES, &len);
        if (bytes && len > 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) message_set_thumbnail(m, bytes, len);   /* JPEG only */
        free(bytes);
    }
}

int json_protocol_decode(const char *line, Event *e) {
    cJSON *j = cJSON_Parse(line);
    if (!j) return -1;
    const char *evt = get_str(j, "evt");
    int ok = 0;
    event_init(e, EVENT_NONE);

    if (!strcmp(evt, "message")) {
        e->type = EVENT_MESSAGE_UPSERT;
        decode_message(j, e);
        ok = e->message.id[0] && e->message.chat_jid[0];
    } else if (!strcmp(evt, "status")) {
        e->type = EVENT_MESSAGE_STATUS;
        copy_line(e->id, sizeof(e->id), get_str(j, "id"));
        e->status = message_status_parse(get_str(j, "status"));
        ok = e->id[0] != '\0';
    } else if (!strcmp(evt, "receipt")) {
        e->type = EVENT_MESSAGE_RECEIPT;
        copy_line(e->id, sizeof(e->id), get_str(j, "id"));
        copy_line(e->jid, sizeof(e->jid), get_str(j, "by"));
        e->receipt = receipt_kind_parse(get_str(j, "kind"));
        e->at = (int64_t)get_num(j, "at", 0);
        ok = e->id[0] && e->jid[0] && e->receipt != RECEIPT_NONE && e->at > 0;
    } else if (!strcmp(evt, "chat")) {
        e->type = EVENT_CHAT_UPDATE;
        chat_init(&e->chat, get_str(j, "jid"));
        copy_line(e->chat.jid, sizeof(e->chat.jid), e->chat.jid);
        copy_line(e->chat.name, sizeof(e->chat.name), get_str(j, "name"));
        copy_line(e->chat.preview, sizeof(e->chat.preview), get_str(j, "preview"));
        e->chat.last_ts = (int64_t)get_num(j, "ts", 0);
        e->chat.unread = (int)get_num(j, "unread", -1);
        const cJSON *archived = cJSON_GetObjectItemCaseSensitive(j, "archived");
        const cJSON *locked = cJSON_GetObjectItemCaseSensitive(j, "locked");
        e->chat.is_archived = cJSON_IsBool(archived) ? cJSON_IsTrue(archived) : -1;
        e->chat.is_locked = cJSON_IsBool(locked) ? cJSON_IsTrue(locked) : -1;
        ok = e->chat.jid[0] != '\0';
    } else if (!strcmp(evt, "contact")) {
        e->type = EVENT_CONTACT_UPDATE;
        contact_init(&e->contact, "");
        copy_line(e->contact.jid, sizeof(e->contact.jid), get_str(j, "jid"));
        copy_line(e->contact.name, sizeof(e->contact.name), get_str(j, "name"));
        copy_line(e->contact.push_name, sizeof(e->contact.push_name), get_str(j, "push_name"));
        ok = e->contact.jid[0] != '\0';
    } else if (!strcmp(evt, "qr")) {
        e->type = EVENT_AUTH_QR;
        e->qr_ascii = dup_text(get_str(j, "ascii"), MAX_QR_BYTES);
        ok = e->qr_ascii != NULL;
    } else if (!strcmp(evt, "pairing_code")) {
        e->type = EVENT_AUTH_PAIRING_CODE;
        copy_line(e->code, sizeof(e->code), get_str(j, "code"));
        ok = e->code[0] != '\0';
    } else if (!strcmp(evt, "auth_required")) {
        e->type = EVENT_AUTH_REQUIRED;
        ok = 1;
    } else if (!strcmp(evt, "connected")) {
        e->type = EVENT_AUTH_CONNECTED;
        copy_line(e->jid, sizeof(e->jid), get_str(j, "jid"));
        copy_line(e->name, sizeof(e->name), get_str(j, "name"));
        ok = 1;
    } else if (!strcmp(evt, "logged_out")) {
        e->type = EVENT_AUTH_LOGGED_OUT;
        ok = 1;
    } else if (!strcmp(evt, "connection")) {
        e->type = EVENT_CONNECTION_STATUS;
        copy_line(e->reason, sizeof(e->reason), get_str(j, "reason"));
        copy_line(e->detail, sizeof(e->detail), get_str(j, "detail"));
        ok = e->reason[0] != '\0';
    } else if (!strcmp(evt, "media")) {
        e->type = EVENT_MEDIA_READY;
        copy_line(e->id, sizeof(e->id), get_str(j, "id"));
        str_copy(e->path, sizeof(e->path), get_str(j, "path"));
        ok = e->id[0] && e->path[0];
    } else if (!strcmp(evt, "edit")) {
        e->type = EVENT_MESSAGE_EDIT;
        copy_line(e->message.id, sizeof(e->message.id), get_str(j, "id"));
        copy_line(e->message.chat_jid, sizeof(e->message.chat_jid), get_str(j, "chat"));
        e->message.deleted = get_bool(j, "deleted");
        const char *text = get_str(j, "text");
        if (text[0]) e->message.text = dup_text(text, MAX_TEXT_BYTES);
        ok = e->message.id[0] != '\0';
    } else if (!strcmp(evt, "call")) {
        e->type = EVENT_CALL;
        copy_line(e->id, sizeof(e->id), get_str(j, "id"));
        copy_line(e->jid, sizeof(e->jid), get_str(j, "from"));
        copy_line(e->state, sizeof(e->state), get_str(j, "state"));
        e->video = get_bool(j, "video");
        e->group = get_bool(j, "group");
        ok = e->id[0] && e->jid[0] && e->state[0];
    } else if (!strcmp(evt, "profile")) {
        e->type = EVENT_PROFILE;
        ok = decode_profile(j, e);
    } else if (!strcmp(evt, "picture")) {
        e->type = EVENT_PICTURE;
        copy_line(e->jid, sizeof(e->jid), get_str(j, "jid"));
        str_copy(e->path, sizeof(e->path), get_str(j, "path"));
        e->full = get_bool(j, "full");
        e->none = get_bool(j, "none");
        ok = e->jid[0] && (e->none || e->path[0]);
    } else if (!strcmp(evt, "picture_changed")) {
        e->type = EVENT_PICTURE_CHANGED;
        copy_line(e->jid, sizeof(e->jid), get_str(j, "jid"));
        ok = e->jid[0] != '\0';
    } else if (!strcmp(evt, "blocklist")) {
        e->type = EVENT_BLOCKLIST;
        ok = decode_blocklist(j, e);
    } else if (!strcmp(evt, "chat_removed")) {
        e->type = EVENT_CHAT_REMOVED;
        chat_init(&e->chat, get_str(j, "jid"));
        ok = e->chat.jid[0] != '\0';
    } else if (!strcmp(evt, "removed")) {
        e->type = EVENT_MESSAGE_REMOVED;
        copy_line(e->message.id, sizeof(e->message.id), get_str(j, "id"));
        copy_line(e->message.chat_jid, sizeof(e->message.chat_jid), get_str(j, "chat"));
        ok = e->message.id[0] != '\0';
    } else if (!strcmp(evt, "reaction")) {
        e->type = EVENT_REACTION;
        copy_line(e->id, sizeof(e->id), get_str(j, "id"));
        copy_line(e->chat.jid, sizeof(e->chat.jid), get_str(j, "chat"));
        copy_line(e->jid, sizeof(e->jid), get_str(j, "sender"));
        copy_line(e->emoji, sizeof(e->emoji), get_str(j, "emoji"));
        ok = e->id[0] && e->jid[0];
    } else if (!strcmp(evt, "typing")) {
        e->type = EVENT_TYPING;
        copy_line(e->chat.jid, sizeof(e->chat.jid), get_str(j, "chat"));
        copy_line(e->jid, sizeof(e->jid), get_str(j, "sender"));
        copy_line(e->state, sizeof(e->state), get_str(j, "state"));
        ok = e->chat.jid[0] && e->state[0];
    } else if (!strcmp(evt, "presence")) {
        e->type = EVENT_PRESENCE;
        copy_line(e->jid, sizeof(e->jid), get_str(j, "jid"));
        copy_line(e->chat.jid, sizeof(e->chat.jid), e->jid);
        copy_line(e->state, sizeof(e->state), get_str(j, "state"));
        e->at = (int64_t)get_num(j, "last_seen", 0);
        ok = e->jid[0] && e->state[0];
    } else if (!strcmp(evt, "alias")) {
        e->type = EVENT_JID_ALIAS;
        copy_line(e->lid, sizeof(e->lid), get_str(j, "lid"));
        copy_line(e->jid, sizeof(e->jid), get_str(j, "pn"));
        ok = e->lid[0] && e->jid[0] && strcmp(e->lid, e->jid) != 0;
    } else if (!strcmp(evt, "profile_updated")) {
        e->type = EVENT_PROFILE_UPDATED;
        copy_line(e->reason, sizeof(e->reason), get_str(j, "field"));
        copy_line(e->detail, sizeof(e->detail), get_str(j, "detail"));
        copy_line(e->name, sizeof(e->name), get_str(j, "name"));
        e->ok = get_bool(j, "ok");
        ok = e->reason[0] != '\0';
    } else if (!strcmp(evt, "link")) {
        e->type = EVENT_LINK_PREVIEW;
        copy_line(e->message.id, sizeof(e->message.id), get_str(j, "id"));
        const char *url = get_str(j, "url");
        if (!strncmp(url, "https://", 8) || !strncmp(url, "http://", 7)) {
            char title[256], desc[512];
            copy_line(title, sizeof(title), get_str(j, "title"));
            copy_line(desc, sizeof(desc), get_str(j, "desc"));
            message_set_link(&e->message, link_preview_create(url, title, desc));
        }
        const char *thumb = get_str(j, "thumb");
        if (thumb[0]) {
            int len = 0;
            unsigned char *bytes = base64_decode(thumb, MAX_THUMB_BYTES, &len);
            if (bytes && len > 2 && bytes[0] == 0xFF && bytes[1] == 0xD8) message_set_thumbnail(&e->message, bytes, len);
            free(bytes);
        }
        ok = e->message.id[0] != '\0' && e->message.link != NULL;
    } else if (!strcmp(evt, "status_posted")) {
        e->type = EVENT_STATUS_POSTED;
        copy_line(e->id, sizeof(e->id), get_str(j, "id"));
        copy_line(e->detail, sizeof(e->detail), get_str(j, "detail"));
        e->ok = get_bool(j, "ok");
        ok = e->id[0] != '\0';
    } else if (!strcmp(evt, "error")) {
        e->type = EVENT_ERROR;
        copy_line(e->id, sizeof(e->id), get_str(j, "id"));
        copy_line(e->detail, sizeof(e->detail), get_str(j, "detail"));
        ok = 1;
    }
    cJSON_Delete(j);
    if (!ok) event_dispose(e);
    return ok ? 0 : -1;
}

static char *finish(cJSON *obj) {
    char *out = obj ? cJSON_PrintUnformatted(obj) : NULL;
    cJSON_Delete(obj);
    return out;
}

static cJSON *command(const char *name) {
    cJSON *obj = cJSON_CreateObject();
    if (obj) cJSON_AddStringToObject(obj, "cmd", name);
    return obj;
}

char *json_protocol_encode_connect(void) { return finish(command("connect")); }
char *json_protocol_encode_reconnect(void) { return finish(command("reconnect")); }
char *json_protocol_encode_qr(void)      { return finish(command("qr")); }
char *json_protocol_encode_logout(void)  { return finish(command("logout")); }

/* "mentions": the JIDs of the people mentioned, when there are any. */
static void add_mentions(cJSON *o, const MentionList *mentions) {
    if (!mentions || mentions->count == 0) return;
    cJSON *list = cJSON_AddArrayToObject(o, "mentions");
    for (int i = 0; i < mentions->count; i++) cJSON_AddItemToArray(list, cJSON_CreateString(mentions->items[i].jid));
}

static void add_forwarding(cJSON *o, int forwarded, int score) {
    if (!forwarded) return;
    cJSON_AddBoolToObject(o, "forwarded", 1);
    cJSON_AddNumberToObject(o, "forwarding_score", score > 0 ? score : 1);
}

char *json_protocol_encode_send(const char *jid, const OutgoingText *text, const char *message_id) {
    cJSON *o = command("send");
    const QuoteRef *quote = text->quote;
    cJSON_AddStringToObject(o, "jid", jid);
    cJSON_AddStringToObject(o, "text", text->text ? text->text : "");
    cJSON_AddStringToObject(o, "id", message_id);
    add_mentions(o, text->mentions);
    add_forwarding(o, text->forwarded, text->forwarding_score);
    if (text->want_link_preview) cJSON_AddBoolToObject(o, "link_preview", 1);
    if (quote && quote->id[0]) {
        cJSON *q = cJSON_AddObjectToObject(o, "reply_to");
        cJSON_AddStringToObject(q, "id", quote->id);
        cJSON_AddStringToObject(q, "sender", quote->sender);
        cJSON_AddStringToObject(q, "text", quote->text);
        if (quote->is_status) cJSON_AddBoolToObject(q, "status", 1);
    }
    return finish(o);
}

char *json_protocol_encode_like_status(const char *author, const char *status_id, const char *emoji) {
    cJSON *o = command("like_status");
    cJSON_AddStringToObject(o, "jid", author);
    cJSON_AddStringToObject(o, "id", status_id);
    cJSON_AddStringToObject(o, "emoji", emoji ? emoji : "");
    return finish(o);
}

char *json_protocol_encode_edit(const char *jid, const char *message_id, const char *text) {
    cJSON *o = command("edit");
    cJSON_AddStringToObject(o, "jid", jid);
    cJSON_AddStringToObject(o, "id", message_id);
    cJSON_AddStringToObject(o, "text", text);
    return finish(o);
}

char *json_protocol_encode_delete(const DeleteRequest *r) {
    cJSON *o = command("delete");
    cJSON_AddStringToObject(o, "jid", r->chat);
    cJSON_AddStringToObject(o, "id", r->id);
    if (r->sender[0]) cJSON_AddStringToObject(o, "sender", r->sender);
    cJSON_AddBoolToObject(o, "from_me", r->from_me != 0);
    cJSON_AddBoolToObject(o, "everyone", r->everyone != 0);
    cJSON_AddNumberToObject(o, "ts", (double)r->timestamp);
    return finish(o);
}

char *json_protocol_encode_delete_chat(const DeleteRequest *r) {
    cJSON *o = command("delete_chat");
    cJSON_AddStringToObject(o, "jid", r->chat);
    if (r->id[0]) cJSON_AddStringToObject(o, "id", r->id);
    cJSON_AddBoolToObject(o, "from_me", r->from_me != 0);
    cJSON_AddNumberToObject(o, "ts", (double)r->timestamp);
    return finish(o);
}

char *json_protocol_encode_reject_call(const char *from, const char *call_id) {
    cJSON *o = command("reject_call");
    cJSON_AddStringToObject(o, "jid", from);
    cJSON_AddStringToObject(o, "id", call_id);
    return finish(o);
}

char *json_protocol_encode_profile(const char *jid) {
    cJSON *o = command("profile");
    cJSON_AddStringToObject(o, "jid", jid);
    return finish(o);
}

char *json_protocol_encode_picture(const char *jid, int full) {
    cJSON *o = command("picture");
    cJSON_AddStringToObject(o, "jid", jid);
    cJSON_AddBoolToObject(o, "full", full != 0);
    return finish(o);
}

char *json_protocol_encode_block(const char *jid, int block) {
    cJSON *o = command("block");
    cJSON_AddStringToObject(o, "jid", jid);
    cJSON_AddBoolToObject(o, "block", block != 0);
    return finish(o);
}

char *json_protocol_encode_react(const ReactionTarget *target, const char *emoji) {
    cJSON *o = command("react");
    cJSON_AddStringToObject(o, "jid", target->chat);
    cJSON_AddStringToObject(o, "id", target->id);
    cJSON_AddStringToObject(o, "sender", target->sender);
    cJSON_AddBoolToObject(o, "from_me", target->from_me);
    cJSON_AddStringToObject(o, "emoji", emoji ? emoji : "");
    return finish(o);
}

char *json_protocol_encode_typing(const char *jid, const char *state) {
    cJSON *o = command("typing");
    cJSON_AddStringToObject(o, "jid", jid);
    cJSON_AddStringToObject(o, "state", state);
    return finish(o);
}

char *json_protocol_encode_subscribe(const char *jid) {
    cJSON *o = command("subscribe");
    cJSON_AddStringToObject(o, "jid", jid);
    return finish(o);
}

char *json_protocol_encode_history(const HistoryAnchor *a, int count) {
    cJSON *o = command("history");
    cJSON_AddStringToObject(o, "jid", a->chat);
    cJSON_AddStringToObject(o, "id", a->id);
    cJSON_AddNumberToObject(o, "ts", (double)a->timestamp);
    cJSON_AddBoolToObject(o, "from_me", a->from_me);
    cJSON_AddNumberToObject(o, "count", count);
    return finish(o);
}

char *json_protocol_encode_presence(int available) {
    cJSON *o = command("presence");
    cJSON_AddBoolToObject(o, "available", available);
    return finish(o);
}

char *json_protocol_encode_send_voice(const char *jid, const char *path, int seconds, const char *message_id) {
    cJSON *o = command("send_voice");
    cJSON_AddStringToObject(o, "jid", jid);
    cJSON_AddStringToObject(o, "path", path);
    cJSON_AddNumberToObject(o, "seconds", seconds);
    cJSON_AddStringToObject(o, "id", message_id);
    return finish(o);
}

char *json_protocol_encode_send_media(const char *jid, const OutgoingMedia *media, const char *message_id) {
    cJSON *o = command("send_media");
    cJSON_AddStringToObject(o, "jid", jid);
    cJSON_AddStringToObject(o, "path", media->path);
    cJSON_AddStringToObject(o, "kind", media->kind);
    cJSON_AddStringToObject(o, "mime", media->mime);
    cJSON_AddStringToObject(o, "file_name", media->file_name ? media->file_name : "");
    cJSON_AddStringToObject(o, "text", media->caption ? media->caption : "");
    cJSON_AddStringToObject(o, "id", message_id);
    add_forwarding(o, media->forwarded, media->forwarding_score);
    return finish(o);
}

char *json_protocol_encode_forward_media(const char *jid, const char *media_ref, int forwarding_score, const char *message_id) {
    cJSON *o = command("forward_media");
    cJSON_AddStringToObject(o, "jid", jid);
    cJSON_AddStringToObject(o, "ref", media_ref);
    cJSON_AddStringToObject(o, "id", message_id);
    add_forwarding(o, 1, forwarding_score);
    return finish(o);
}

char *json_protocol_encode_set_name(const char *name) {
    cJSON *o = command("set_name");
    cJSON_AddStringToObject(o, "name", name ? name : "");
    return finish(o);
}

char *json_protocol_encode_set_about(const char *text) {
    cJSON *o = command("set_about");
    cJSON_AddStringToObject(o, "text", text ? text : "");
    return finish(o);
}

char *json_protocol_encode_set_picture(const char *path) {
    cJSON *o = command("set_picture");
    cJSON_AddStringToObject(o, "path", path ? path : "");
    return finish(o);
}

char *json_protocol_encode_remove_picture(void) { return finish(command("remove_picture")); }

char *json_protocol_encode_post_status(const StatusPost *post) {
    cJSON *o = command("post_status");
    cJSON_AddStringToObject(o, "kind", status_kind_name(post->kind));
    cJSON_AddStringToObject(o, "text", post->text);
    cJSON_AddStringToObject(o, "path", post->path);
    cJSON_AddStringToObject(o, "mime", post->mime);
    cJSON_AddNumberToObject(o, "bg", (double)post->background_argb);
    cJSON_AddNumberToObject(o, "font", post->font);
    cJSON_AddStringToObject(o, "id", post->id);
    return finish(o);
}

char *json_protocol_encode_pair(const char *phone_digits) {
    cJSON *o = command("pair");
    cJSON_AddStringToObject(o, "phone", phone_digits);
    return finish(o);
}

char *json_protocol_encode_download(const char *message_id, const char *media_ref, int max_mb) {
    cJSON *o = command("download");
    cJSON_AddStringToObject(o, "id", message_id);
    cJSON_AddStringToObject(o, "ref", media_ref ? media_ref : "");
    cJSON_AddNumberToObject(o, "max_mb", max_mb);
    return finish(o);
}

char *json_protocol_encode_read(const ReadRequest *r) {
    cJSON *o = command("read");
    cJSON_AddStringToObject(o, "jid", r->chat_jid);
    cJSON_AddBoolToObject(o, "receipts", r->send_receipts);
    cJSON *items = cJSON_AddArrayToObject(o, "messages");
    for (int i = 0; i < r->count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", r->items[i].id);
        cJSON_AddStringToObject(item, "sender", r->items[i].sender);
        cJSON_AddItemToArray(items, item);
    }
    if (r->last_id[0]) {
        cJSON *last = cJSON_AddObjectToObject(o, "last");
        cJSON_AddStringToObject(last, "id", r->last_id);
        cJSON_AddStringToObject(last, "sender", r->last_sender);
        cJSON_AddBoolToObject(last, "from_me", r->last_from_me);
        cJSON_AddNumberToObject(last, "ts", (double)r->last_timestamp);
    }
    return finish(o);
}

char *json_protocol_encode_init(const char *auth_dir, const char *media_dir, const char *log_dir, int debug) {
    cJSON *o = command("init");
    cJSON_AddStringToObject(o, "log_dir", log_dir);
    cJSON_AddStringToObject(o, "auth_dir", auth_dir);
    cJSON_AddStringToObject(o, "media_dir", media_dir);
    cJSON_AddBoolToObject(o, "debug", debug);
    return finish(o);
}
