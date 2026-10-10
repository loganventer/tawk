/* Managing chats over the control socket: mute, pin, archive, lock, theme,
 * clear, delete, export and block. */
#include "control_server_state.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int resolve(ControlServer *s, ControlSession *session, const ControlRequest *req, Chat *out) {
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    int at = control_resolve_chat(s, session, req, "chat", all, n);
    if (at < 0) return -1;
    *out = all[at];
    return 0;
}

static void start(ControlPending *p, const ControlRequest *req, const char *op, WriteKind kind, ControlExecute run, const Chat *chat) {
    control_pending_init(p, req->id, op, kind, run);
    str_copy(p->chat_jid, sizeof(p->chat_jid), chat->jid);
}

/* ---- set_chat ---- */

static const Chat *current(ControlServer *s, const char *jid) {
    int n = 0;
    const Chat *all = messaging_manager_chats(s->deps.messaging, &n);
    for (int i = 0; i < n; i++) if (!strcmp(all[i].jid, jid)) return &all[i];
    return NULL;
}

static cJSON *do_set_chat(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    MessagingManager *mm = s->deps.messaging;
    const cJSON *muted = cJSON_GetObjectItemCaseSensitive(p->args, "muted");
    if (muted) {
        int64_t until = cJSON_IsNumber(muted) ? (int64_t)time(NULL) + (int64_t)muted->valuedouble : cJSON_IsTrue(muted) ? -1 : 0;
        messaging_manager_mute_until(mm, p->chat_jid, until);
    }
    const Chat *c = current(s, p->chat_jid);
    const cJSON *pinned = cJSON_GetObjectItemCaseSensitive(p->args, "pinned");
    if (c && cJSON_IsBool(pinned) && (c->is_pinned != 0) != cJSON_IsTrue(pinned)) messaging_manager_toggle_pin(mm, p->chat_jid);
    const cJSON *archived = cJSON_GetObjectItemCaseSensitive(p->args, "archived");
    if (cJSON_IsBool(archived)) messaging_manager_set_archived(mm, p->chat_jid, cJSON_IsTrue(archived));
    c = current(s, p->chat_jid);
    if (c && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(p->args, "locked")) && !c->soft_locked) {
        messaging_manager_toggle_soft_lock(mm, p->chat_jid);
    }
    c = current(s, p->chat_jid);
    if (!c) { str_copy(f->why, sizeof(f->why), "The chat is gone"); return NULL; }
    cJSON *r = cJSON_CreateObject();
    if (automation_manager_chat_allowed(s->deps.automation, c)) cJSON_AddItemToObject(r, "chat", control_chat_json(s, p->origin, c));
    return r;
}

static void append(char *out, size_t size, const char *part) {
    size_t used = strlen(out);
    snprintf(out + used, size - used, "%s%s", used ? ", " : "", part);
}

void control_op_set_chat(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    ControlPending p;
    start(&p, req, "set_chat", WRITE_KIND_MANAGE, do_set_chat, &chat);
    char action[128] = "";
    const cJSON *muted = cJSON_GetObjectItemCaseSensitive(req->args, "muted");
    if (cJSON_IsBool(muted)) {
        cJSON_AddBoolToObject(p.args, "muted", cJSON_IsTrue(muted));
        append(action, sizeof(action), cJSON_IsTrue(muted) ? "mute it always" : "unmute it");
    } else if (cJSON_IsNumber(muted) && muted->valuedouble > 0) {
        cJSON_AddNumberToObject(p.args, "muted", muted->valuedouble);
        char part[48];
        double h = muted->valuedouble / 3600.0;
        snprintf(part, sizeof(part), h >= 48 ? "mute it for %.0f days" : "mute it for %.0f hours", h >= 48 ? h / 24 : h);
        append(action, sizeof(action), part);
    }
    const char *flags[] = { "pinned", "archived" };
    const char *yes[] = { "pin it", "archive it" }, *no[] = { "unpin it", "unarchive it" };
    for (int i = 0; i < 2; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(req->args, flags[i]);
        if (!cJSON_IsBool(v)) continue;
        cJSON_AddBoolToObject(p.args, flags[i], cJSON_IsTrue(v));
        append(action, sizeof(action), cJSON_IsTrue(v) ? yes[i] : no[i]);
    }
    const cJSON *locked = cJSON_GetObjectItemCaseSensitive(req->args, "locked");
    if (cJSON_IsFalse(locked)) {
        control_pending_dispose(&p);
        control_fail(s, session->conn, req->id, "not_allowed", "Only you can unlock a chat, in tawk");
        return;
    }
    if (cJSON_IsTrue(locked)) {
        cJSON_AddBoolToObject(p.args, "locked", 1);
        append(action, sizeof(action), "hide it behind the lock");
    }
    if (!action[0]) {
        control_pending_dispose(&p);
        control_fail(s, session->conn, req->id, "bad_request", "Give at least one of muted, pinned, archived or locked");
        return;
    }
    str_copy(p.action, sizeof(p.action), action);
    control_write(s, session, &p);
}

/* ---- set_chat_theme ---- */

static cJSON *do_theme(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    (void)f;
    messaging_manager_set_chat_theme(s->deps.messaging, p->chat_jid, control_codec_string(p->args, "theme"));
    return cJSON_CreateObject();
}

void control_op_set_chat_theme(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    const char *theme = control_codec_string(req->args, "theme");
    if (!theme) { control_fail(s, session->conn, req->id, "bad_request", "\"theme\" is required (\"\" for the app theme)"); return; }
    IThemeRepository *themes = settings_manager_themes(s->deps.settings);
    if (theme[0] && themes->index_of(themes, theme) < 0) {
        control_fail(s, session->conn, req->id, "not_found", "No such theme (see list_themes)");
        return;
    }
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    ControlPending p;
    start(&p, req, "set_chat_theme", WRITE_KIND_MANAGE, do_theme, &chat);
    cJSON_AddStringToObject(p.args, "theme", theme);
    if (theme[0]) snprintf(p.action, sizeof(p.action), "give it the %s theme", theme);
    else str_copy(p.action, sizeof(p.action), "give it the app's theme again");
    control_write(s, session, &p);
}

/* ---- clear_chat and delete_chat ---- */

static cJSON *do_clear(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    if (messaging_manager_clear_chat(s->deps.messaging, p->chat_jid) != 0) { str_copy(f->why, sizeof(f->why), "The chat could not be cleared"); return NULL; }
    return cJSON_CreateObject();
}

void control_op_clear_chat(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    ControlPending p;
    start(&p, req, "clear_chat", WRITE_KIND_DESTRUCTIVE, do_clear, &chat);
    str_copy(p.action, sizeof(p.action), "remove every message of this chat from this computer");
    control_write(s, session, &p);
}

static cJSON *do_delete(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    if (messaging_manager_delete_chat(s->deps.messaging, p->chat_jid) != 0) { str_copy(f->why, sizeof(f->why), "The chat could not be deleted"); return NULL; }
    return cJSON_CreateObject();
}

void control_op_delete_chat(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    ControlPending p;
    start(&p, req, "delete_chat", WRITE_KIND_DESTRUCTIVE, do_delete, &chat);
    str_copy(p.action, sizeof(p.action), "delete the whole chat, here and on your phone");
    p.needs_connection = 1;
    control_write(s, session, &p);
}

/* ---- export_chat ---- */

static cJSON *do_export(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    const Settings *st = control_settings(s);
    char dir[512], out[1100];
    if (st->download_dir[0]) path_expand_home(st->download_dir, dir, sizeof(dir));
    else path_download_dir(dir, sizeof(dir));
    int with_media = control_codec_bool(p->args, "with_media", 0);
    if (messaging_manager_export_chat(s->deps.messaging, p->chat_jid, dir, with_media, out, sizeof(out)) != 0) {
        str_copy(f->why, sizeof(f->why), "The chat could not be exported");
        return NULL;
    }
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "path", out);
    return r;
}

void control_op_export_chat(ControlServer *s, ControlSession *session, const ControlRequest *req) {
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    int with_media = control_codec_bool(req->args, "with_media", 0);
    ControlPending p;
    start(&p, req, "export_chat", WRITE_KIND_MANAGE, do_export, &chat);
    cJSON_AddBoolToObject(p.args, "with_media", with_media);
    str_copy(p.action, sizeof(p.action), with_media ? "export the chat with its media to your downloads folder"
                                                    : "export the chat to your downloads folder");
    control_write(s, session, &p);
}

/* ---- block and unblock ---- */

static cJSON *do_block(ControlServer *s, const ControlPending *p, ControlFailure *f) {
    int blocked = control_codec_bool(p->args, "blocked", 1);
    if (!s->deps.profiles || profile_manager_set_blocked(s->deps.profiles, p->chat_jid, blocked) != 0) {
        str_copy(f->why, sizeof(f->why), blocked ? "They could not be blocked" : "They could not be unblocked");
        return NULL;
    }
    return cJSON_CreateObject();
}

static void block_op(ControlServer *s, ControlSession *session, const ControlRequest *req, int blocked) {
    Chat chat;
    if (resolve(s, session, req, &chat) != 0) return;
    if (chat.is_group) { control_fail(s, session->conn, req->id, "bad_request", "Only people can be blocked"); return; }
    ControlPending p;
    start(&p, req, blocked ? "block" : "unblock", blocked ? WRITE_KIND_DESTRUCTIVE : WRITE_KIND_MANAGE, do_block, &chat);
    cJSON_AddBoolToObject(p.args, "blocked", blocked);
    str_copy(p.action, sizeof(p.action), blocked ? "block this person" : "unblock this person");
    p.needs_connection = 1;
    control_write(s, session, &p);
}

void control_op_block(ControlServer *s, ControlSession *session, const ControlRequest *req) { block_op(s, session, req, 1); }
void control_op_unblock(ControlServer *s, ControlSession *session, const ControlRequest *req) { block_op(s, session, req, 0); }
