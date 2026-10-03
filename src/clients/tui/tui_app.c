#include "tui_app_state.h"

#include "clients/tui/color_pair_cache.h"
#include "clients/tui/escape_sequence.h"
#include "engines/media_type_detector.h"
#include "core/message_file_name.h"
#include "clients/tui/tui_palette.h"
#include "core/contact.h"
#include "utilities/app_info.h"
#include "utilities/clock_util.h"
#include "utilities/dropped_path.h"
#include "utilities/log.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"
#include "utilities/utf8_text.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TOAST_MS          3500
#define READER_MIN_LINES  8         /* Enter opens long text messages in the reader */

/* Extra terminal modes curses does not manage: button-drag mouse tracking
 * (divider resizing) and bracketed paste (file drops). */
static void terminal_modes(int enable, int mouse) {
    if (enable) {
        if (mouse) fputs("\033[?1002h", stdout);
        fputs("\033[?2004h", stdout);
        fputs(escape_sequence_enable(), stdout);      /* so Ctrl+Shift+L differs from Ctrl+L */
        fputs("\033[5 q", stdout);                    /* a blinking bar cursor in text fields */
    } else {
        fputs("\033[?1002l\033[?1000l\033[?2004l", stdout);
        fputs(escape_sequence_disable(), stdout);
        fputs("\033[0 q", stdout);                    /* the terminal's own cursor style again */
    }
    fflush(stdout);
}

static const Settings *settings(TuiApp *app) { return settings_manager_current(app->deps.settings); }

void tui_app_invalidate(TuiApp *app) { app->dirty = 1; }

void tui_app_toast(TuiApp *app, const char *text, int is_error) {
    app->dirty = 1;
    str_copy(app->toast, sizeof(app->toast), text);
    app->toast_error = is_error;
    app->toast_until_ms = clock_now_ms() + TOAST_MS;
}

void tui_app_quit(TuiApp *app) { app->running = 0; }

/* ---- names -------------------------------------------------------------- */

static void resolve_name(void *ctx, const char *jid, char *out, size_t size) {
    TuiApp *app = ctx;
    const char *me = messaging_manager_user_jid(app->deps.messaging);
    if (me[0] && jid && strcmp(jid, me) == 0) { str_copy(out, size, "You"); return; }
    int count = 0;
    const Chat *chats = messaging_manager_chats(app->deps.messaging, &count);
    for (int i = 0; jid && i < count; i++) {
        if (strcmp(chats[i].jid, jid) == 0 && chats[i].name[0]) { str_copy(out, size, chats[i].name); return; }
    }
    messaging_manager_display_name(app->deps.messaging, jid, out, size);
}

const NameResolver *tui_app_names(TuiApp *app) {
    static NameResolver resolver;
    resolver.ctx = app;
    resolver.resolve = resolve_name;
    return &resolver;
}

/* ---- login -------------------------------------------------------------- */

int tui_app_show_login(TuiApp *app) {
    AuthState auth = messaging_manager_auth_state(app->deps.messaging);
    int chats = 0;
    messaging_manager_chats(app->deps.messaging, &chats);
    if (auth == AUTH_STATE_NEEDS_LOGIN) return 1;
    if (chats > 0) return 0;
    return auth == AUTH_STATE_STARTING || clock_now_ms() < app->linked_until_ms;
}

/* Drives the linking wizard from connection state changes. */
static void follow_auth(TuiApp *app, int64_t now) {
    AuthState auth = messaging_manager_auth_state(app->deps.messaging);
    if (auth == app->last_auth) return;
    if (auth == AUTH_STATE_NEEDS_LOGIN) {
        login_view_show(&app->login, LOGIN_STEP_WELCOME);
    } else if (auth == AUTH_STATE_CONNECTED && app->last_auth == AUTH_STATE_NEEDS_LOGIN) {
        login_view_show(&app->login, LOGIN_STEP_LINKED);
        app->linked_until_ms = now + 30000;
    }
    app->last_auth = auth;
}

/* ---- chats, drafts and themes ------------------------------------------- */

static void save_draft(TuiApp *app) {
    const char *jid = messaging_manager_open_jid(app->deps.messaging);
    if (!jid[0]) return;
    char *text = composer_view_text(&app->composer);
    messaging_manager_save_draft(app->deps.messaging, jid, text ? str_trim(text) : "");
    free(text);
}

/* Profile pictures for the views, through the profile manager. */
static const char *portrait_of(void *ctx, const char *jid) {
    return profile_manager_picture(((TuiApp *)ctx)->deps.profiles, jid);
}

/* Applies the open chat's theme to the conversation pane (or the app theme). */
static void sync_conversation_theme(TuiApp *app) {
    const Chat *chat = messaging_manager_open_chat_info(app->deps.messaging);
    const char *want = chat ? chat->theme : "";
    if (app->theme_picker.open) return;                      /* the picker is previewing */
    if (app->conversation_theme_valid && strcmp(want, app->conversation_theme) == 0) return;
    IThemeRepository *themes = settings_manager_themes(app->deps.settings);
    int index = want[0] ? themes->index_of(themes, want) : -1;
    tui_palette_apply_conversation(index >= 0 ? themes->at(themes, index) : settings_manager_theme(app->deps.settings));
    str_copy(app->conversation_theme, sizeof(app->conversation_theme), want);
    app->conversation_theme_valid = 1;
    app->dirty = 1;
}

void tui_app_sync_conversation_theme(TuiApp *app, int force) {
    if (force) app->conversation_theme_valid = 0;
    sync_conversation_theme(app);
}

void tui_app_cancel_reply(TuiApp *app) {
    memset(&app->reply, 0, sizeof(app->reply));
    app->reply_name[0] = '\0';
}

/* Folding a chat list group is remembered across restarts, whatever folded or unfolded it. */
void tui_app_save_folding(TuiApp *app) {
    const Settings *now = settings(app);
    if (now->pinned_folded == app->chat_list.pinned_collapsed && now->chats_folded == app->chat_list.others_collapsed) return;
    Settings s = *now;
    s.pinned_folded = app->chat_list.pinned_collapsed;
    s.chats_folded = app->chat_list.others_collapsed;
    tui_app_apply_settings(app, &s);
}

/* Opens a chat; with unfold set, a folded group holding it opens so it can be seen. */
static void open_chat(TuiApp *app, const char *jid, int unfold) {
    if (!jid) return;
    const char *current = messaging_manager_open_jid(app->deps.messaging);
    if (strcmp(current, jid) != 0) {
        if (app->editing_id[0]) { app->editing_id[0] = '\0'; composer_view_clear(&app->composer); }
        save_draft(app);
        composer_view_clear(&app->composer);
        tui_app_forget_mentions(app);
        app->attachment[0] = '\0';
        tui_app_cancel_reply(app);
    }
    messaging_manager_open_chat(app->deps.messaging, jid);
    app->chat_list.open_account = app->deps.active_account;
    app->chat_rows_stale = 1;                              /* its unread count just went */
    int count = 0;
    const Chat *chats = tui_app_chat_rows(app, &count);
    chat_list_view_reveal(&app->chat_list, chats, count, jid, unfold);
    tui_app_save_folding(app);
    str_copy(app->chat_list.open_jid, sizeof(app->chat_list.open_jid), jid);
    if (composer_view_is_empty(&app->composer)) {
        char *draft = messaging_manager_load_draft(app->deps.messaging, jid);
        composer_view_set_text(&app->composer, draft);
        free(draft);
    }
    message_view_release(&app->message_view);
    message_view_scroll_to_latest(&app->message_view);
    if (strcmp(app->deps.blink->jid, jid) == 0) app->deps.blink->until_ms = 0;
    app->conversation_theme_valid = 0;
    sync_conversation_theme(app);
    app->focus = TUI_FOCUS_COMPOSER;
    app->restore_pending = 0;                              /* a chat is open; nothing left to restore */
    if (app->deps.roster) account_roster_manager_set_last_chat(app->deps.roster, app->deps.active_account, jid);
    if (app->deps.active_account <= ACCOUNT_ID_FIRST && strcmp(settings(app)->last_chat, jid) != 0) {
        Settings s = *settings(app);                       /* the first account's is also kept where it always was */
        str_copy(s.last_chat, sizeof(s.last_chat), jid);
        settings_manager_apply(app->deps.settings, &s);
    }
}

int tui_app_use_account(TuiApp *app, AccountId account) {
    if (account == ACCOUNT_ID_NONE || account == app->deps.active_account) return 0;
    IAccountDirectory *dir = app->deps.directory;
    const AccountServices *sv = dir ? dir->find(dir, account) : NULL;
    if (!sv) return -1;
    /* What was being written stays with the chat it was for. */
    if (app->editing_id[0]) { app->editing_id[0] = '\0'; composer_view_clear(&app->composer); }
    save_draft(app);
    composer_view_clear(&app->composer);
    tui_app_forget_mentions(app);
    app->attachment[0] = '\0';
    tui_app_cancel_reply(app);
    /* Dialogs that show the old account's things close with it. */
    profile_dialogs_close(&app->profile);
    status_feed_dialogs_close(&app->feed);
    app->scheduled_list.open = 0;
    app->forward_picker.open = 0;
    app->self_chats.open = 0;
    app->contact.open = 0;
    /* An account out of view has no chat open: its unread counts run again. */
    messaging_manager_open_chat(app->deps.messaging, "");

    app->deps.messaging = sv->messaging;
    app->deps.profiles = sv->profiles;
    app->deps.calls = sv->calls;
    app->deps.accounts = sv->accounts;
    app->deps.statuses = sv->statuses;
    app->deps.feed = sv->feed;
    app->deps.scheduling = sv->scheduling;
    app->deps.backend_name = sv->backend_name;
    app->deps.active_account = account;

    app->chat_list.open_jid[0] = '\0';
    app->chat_list.open_account = account;
    message_view_release(&app->message_view);
    app->conversation_theme_valid = 0;
    app->last_auth = (AuthState)-1;                         /* follow_auth looks at the new account afresh */
    app->linked_until_ms = 0;
    app->restore_pending = 0;
    app->chat_rows_stale = 1;
    app->dirty = 1;
    return 0;
}

void tui_app_open_row(TuiApp *app, const Chat *row) {
    if (!row) return;
    char jid[128];
    str_copy(jid, sizeof(jid), row->jid);                  /* the row may be rebuilt while the account changes */
    if (tui_app_use_account(app, row->account) != 0) return;
    open_chat(app, jid, 1);
}

void tui_app_open_chat(TuiApp *app, const char *jid) { open_chat(app, jid, 1); }

/* Opens the chat that was open when tawk last quit, once the chat list has
 * it. Gives up when the list has loaded without it (the chat was deleted)
 * or when it sits in the Locked folder. */
static void restore_last_chat(TuiApp *app) {
    if (!app->restore_pending || tui_app_show_login(app)) return;
    int count = 0;
    const Chat *chats = messaging_manager_chats(app->deps.messaging, &count);
    if (count == 0) return;                                 /* not loaded yet */
    app->restore_pending = 0;
    if (messaging_manager_open_jid(app->deps.messaging)[0]) return;
    char last[128] = "";
    if (app->deps.roster) account_roster_manager_last_chat(app->deps.roster, app->deps.active_account, last, sizeof(last));
    if (!last[0] && app->deps.active_account <= ACCOUNT_ID_FIRST) str_copy(last, sizeof(last), settings(app)->last_chat);
    for (int i = 0; i < count; i++) {
        if (strcmp(chats[i].jid, last) != 0) continue;
        if (chats[i].is_locked != 1) open_chat(app, last, 0);      /* the groups stay folded as they were left */
        app->dirty = 1;
        return;
    }
}

static void apply_settings_now(TuiApp *app, const Settings *updated) {
    char old_theme[sizeof(updated->theme)];
    str_copy(old_theme, sizeof(old_theme), settings(app)->theme);
    int old_mouse = settings(app)->mouse;
    settings_manager_apply(app->deps.settings, updated);
    if (strcmp(old_theme, updated->theme) != 0) {
        tui_palette_apply(settings_manager_theme(app->deps.settings));
        app->conversation_theme_valid = 0;
    }
    app->chat_list.pinned_collapsed = updated->pinned_folded;          /* changed in the settings panel */
    app->chat_list.others_collapsed = updated->chats_folded;
    if (old_mouse != updated->mouse) {
        mousemask(updated->mouse ? (ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION) : 0, NULL);
        terminal_modes(1, updated->mouse);
    }
    app->settings_revision = settings_manager_revision(app->deps.settings);
    str_copy(app->followed_theme, sizeof(app->followed_theme), updated->theme);
    app->followed_mouse = updated->mouse;
    app->dirty = 1;
}

/* Turning on agent access says what it means first, and waits for a yes. */
void tui_app_apply_settings(TuiApp *app, const Settings *updated) {
    if (updated->control_socket && !settings(app)->control_socket) {
        app->pending_settings = *updated;
        confirm_dialog_open(&app->confirm, CONFIRM_ENABLE_AGENTS, "", "Let agents reach tawk?",
                            "Programs of yours, such as tawk-mcp for Claude Code, can then read your chats and ask to act for you.",
                            "Chat text they read goes to their model's provider. A message someone sends you can try to steer them. "
                            "Locked chats stay hidden; every send or change waits for you in the Agents tab (F3), and deletes need two yeses. "
                            "It needs tawk-mcp added to your MCP client (see the manual).",
                            "Turn on", 1);
        app->dirty = 1;
        return;
    }
    apply_settings_now(app, updated);
}

void tui_app_toggle_dnd(TuiApp *app) {
    Settings s = *settings(app);
    s.do_not_disturb = !s.do_not_disturb;
    tui_app_apply_settings(app, &s);
    tui_app_toast(app, s.do_not_disturb ? "\xF0\x9F\x94\x95 Do not disturb is on" : "\xF0\x9F\x94\x94 Notifications are on", 0);
}

/* ---- messages ----------------------------------------------------------- */

static const Message *message_at(TuiApp *app, int index) {
    int count = 0;
    const Message *msgs = messaging_manager_messages(app->deps.messaging, &count);
    return (index >= 0 && index < count) ? &msgs[index] : NULL;
}

static int is_long_text(const Message *m) {
    if (m->type != MESSAGE_TYPE_TEXT || !m->text) return 0;
    TextLine *lines = NULL;
    int n = utf8_wrap(m->text, 60, &lines);
    free(lines);
    return n > READER_MIN_LINES;
}

static void offer_unknown_file(TuiApp *app, int index);

void tui_app_activate_message(TuiApp *app, int index) {
    const Message *m = message_at(app, index);
    if (!m) return;
    if (is_long_text(m)) {
        char title[160];
        resolve_name(app, m->from_me ? messaging_manager_user_jid(app->deps.messaging) : m->sender_jid, title, sizeof(title));
        StyledText styled;
        if (messaging_manager_format_message(app->deps.messaging, m, &styled) == 0) text_reader_open_styled(&app->reader, title, &styled);
        else text_reader_open(&app->reader, title, m->text);
        styled_text_dispose(&styled);
        return;
    }
    if (!message_type_is_openable(m->type)) return;
    /* Files tawk cannot show get an offer to save them instead. */
    if (m->type == MESSAGE_TYPE_DOCUMENT && !media_picture_is_pdf(m) && !app->unknown_offer) {
        char name[256];
        message_file_name(m, name, sizeof(name));
        if (!media_type_known(name)) { offer_unknown_file(app, index); return; }
    }
    app->unknown_offer = 0;
    /* Photos and videos open in the viewer inside tawk (unless set otherwise). */
    const char *viewer = settings(app)->image_viewer;
    if ((!viewer[0] || strcmp(viewer, "builtin") == 0) && image_viewer_can_show(m)) {
        image_viewer_open(&app->viewer, m, &app->media_sources);
        if (m->type != MESSAGE_TYPE_VIDEO && !(m->media_path[0] && access(m->media_path, R_OK) == 0)) {
            messaging_manager_download(app->deps.messaging, m->id);    /* sharpens when it arrives */
        }
        app->dirty = 1;
        return;
    }
    if (m->media_path[0] && access(m->media_path, R_OK) == 0) {
        if (media_manager_activate(app->deps.media, m->media_path, m->type) != 0) {
            tui_app_toast(app, "No viewer or player is available for this file", 1);
        }
    } else if (messaging_manager_download(app->deps.messaging, m->id) == 0) {
        tui_app_toast(app, "Downloading\xE2\x80\xA6 it opens when ready", 0);
    } else {
        tui_app_toast(app, "This media is not available for download", 1);
    }
}

void tui_app_viewer_action(TuiApp *app, ImageViewerAction action) {
    app->dirty = 1;
    if (action != IMAGE_VIEWER_OPEN_OUTSIDE) return;
    int count = 0;
    const Message *msgs = messaging_manager_messages(app->deps.messaging, &count);
    const Message *m = image_viewer_current(&app->viewer, msgs, count, NULL);
    if (!m) return;
    if (m->media_path[0] && access(m->media_path, R_OK) == 0) {
        if (media_manager_activate(app->deps.media, m->media_path, m->type) != 0) {
            tui_app_toast(app, "No viewer or player is available for this file", 1);
        }
    } else if (messaging_manager_download(app->deps.messaging, m->id) == 0) {
        str_copy(app->open_outside_id, sizeof(app->open_outside_id), m->id);
        tui_app_toast(app, "Downloading\xE2\x80\xA6 it opens when ready", 0);
    } else {
        tui_app_toast(app, "This media is not available for download", 1);
    }
}

void tui_app_start_reply(TuiApp *app, int index) {
    const Message *m = message_at(app, index);
    if (!m) return;
    tui_app_cancel_reply(app);
    str_copy(app->reply.id, sizeof(app->reply.id), m->id);
    str_copy(app->reply.sender, sizeof(app->reply.sender), m->from_me ? messaging_manager_user_jid(app->deps.messaging) : m->sender_jid);
    char preview[256];
    message_preview(m, preview, sizeof(preview));
    str_copy(app->reply.text, sizeof(app->reply.text), preview);
    resolve_name(app, app->reply.sender, app->reply_name, sizeof(app->reply_name));
    app->focus = TUI_FOCUS_COMPOSER;
    app->dirty = 1;
}

void tui_app_cancel_edit(TuiApp *app) {
    if (!app->editing_id[0]) return;
    app->editing_id[0] = '\0';
    composer_view_clear(&app->composer);
    app->dirty = 1;
}

void tui_app_start_edit(TuiApp *app, int index) {
    const Message *m = message_at(app, index);
    if (!m || !messaging_manager_can_edit(app->deps.messaging, m)) {
        tui_app_toast(app, "Only your own text messages can be edited, within 15 minutes", 1);
        return;
    }
    tui_app_cancel_reply(app);
    app->attachment[0] = '\0';
    str_copy(app->editing_id, sizeof(app->editing_id), m->id);
    composer_view_set_text(&app->composer, m->text);
    app->focus = TUI_FOCUS_COMPOSER;
    app->dirty = 1;
}

void tui_app_open_message_menu(TuiApp *app, int index, int y, int x) {
    const Message *m = message_at(app, index);
    if (!m) return;
    int enabled[MESSAGE_ACTION_COUNT] = { 0 };
    enabled[MESSAGE_ACTION_REPLY] = !m->deleted;
    enabled[MESSAGE_ACTION_REACT] = !m->deleted;
    enabled[MESSAGE_ACTION_EDIT] = messaging_manager_can_edit(app->deps.messaging, m);
    enabled[MESSAGE_ACTION_COPY] = !m->deleted && m->text && m->text[0];
    enabled[MESSAGE_ACTION_FORWARD] = !m->deleted && m->type != MESSAGE_TYPE_OTHER &&
                                      (m->type != MESSAGE_TYPE_TEXT || (m->text && m->text[0]));
    enabled[MESSAGE_ACTION_OPEN] = !m->deleted && message_type_is_openable(m->type);
    enabled[MESSAGE_ACTION_READ] = !m->deleted && is_long_text(m);
    enabled[MESSAGE_ACTION_RETRY] = m->from_me && m->status == MESSAGE_STATUS_FAILED;
    enabled[MESSAGE_ACTION_SAVE] = !m->deleted && message_type_is_openable(m->type);
    enabled[MESSAGE_ACTION_GOTO_QUOTE] = !m->deleted && m->quoted_id[0];
    enabled[MESSAGE_ACTION_INFO] = m->from_me && !m->deleted;
    enabled[MESSAGE_ACTION_DELETE] = 1;
    app->message_view.selected = index;
    message_menu_open(&app->message_menu, index, y, x, enabled);
}

/* Copies a message's file to Downloads, downloading it first when needed. */
static void save_file(TuiApp *app, const Message *m) {
    char name[256], saved[1024];
    message_file_name(m, name, sizeof(name));
    if (media_manager_save_copy(app->deps.media, m->media_path, name, saved, sizeof(saved)) == 0) {
        char msg[1100];
        snprintf(msg, sizeof(msg), "\xF0\x9F\x92\xBE Saved to %s", saved);
        tui_app_toast(app, msg, 0);
    } else {
        tui_app_toast(app, "Could not save the file (check Settings, Media, Save folder)", 1);
    }
}

void tui_app_save_message(TuiApp *app, int index) {
    const Message *m = message_at(app, index);
    if (!m) return;
    if (m->media_path[0] && access(m->media_path, R_OK) == 0) { save_file(app, m); return; }
    if (messaging_manager_download(app->deps.messaging, m->id) == 0) {
        str_copy(app->save_after_id, sizeof(app->save_after_id), m->id);
        tui_app_toast(app, "Downloading\xE2\x80\xA6 it is saved when it arrives", 0);
    } else {
        tui_app_toast(app, "This file is not available for download", 1);
    }
}

/* A file tawk does not know: offer to save it rather than guess at a viewer. */
static void offer_unknown_file(TuiApp *app, int index) {
    int enabled[MESSAGE_ACTION_COUNT] = { 0 };
    enabled[MESSAGE_ACTION_SAVE] = 1;
    enabled[MESSAGE_ACTION_OPEN] = 1;
    enabled[MESSAGE_ACTION_CANCEL] = 1;
    app->message_view.selected = index;
    message_menu_open(&app->message_menu, index, app->layout.chat.y + app->layout.chat.h / 2,
                      app->layout.chat.x + app->layout.chat.w / 3, enabled);
    for (int i = 0; i < app->message_menu.count; i++) {        /* saving is the safe default */
        if (app->message_menu.items[i] == MESSAGE_ACTION_SAVE) app->message_menu.selected = i;
    }
}

/* The delete choice: for me always, for everyone when WhatsApp still allows it. */
void tui_app_open_delete_menu(TuiApp *app, int index, int y, int x) {
    const Message *m = message_at(app, index);
    if (!m) return;
    int enabled[MESSAGE_ACTION_COUNT] = { 0 };
    enabled[MESSAGE_ACTION_DELETE_FOR_ME] = 1;
    enabled[MESSAGE_ACTION_DELETE_FOR_EVERYONE] = messaging_manager_can_delete_for_everyone(app->deps.messaging, m);
    enabled[MESSAGE_ACTION_CANCEL] = 1;
    app->message_view.selected = index;
    message_menu_open(&app->message_menu, index, y, x, enabled);
}

void tui_app_apply_message_action(TuiApp *app) {
    int index = app->message_menu.message;
    const Message *m = message_at(app, index);
    if (!m) return;
    switch (message_menu_choice(&app->message_menu)) {
        case MESSAGE_ACTION_REPLY: tui_app_start_reply(app, index); break;
        case MESSAGE_ACTION_REACT: tui_app_open_reactions(app, index); break;
        case MESSAGE_ACTION_EDIT:  tui_app_start_edit(app, index); break;
        case MESSAGE_ACTION_COPY:
            if (app->deps.clipboard->copy(app->deps.clipboard, m->text) == 0) tui_app_toast(app, "\xF0\x9F\x93\x8B Copied", 0);
            else tui_app_toast(app, "Could not reach the clipboard", 1);
            break;
        case MESSAGE_ACTION_FORWARD: tui_app_open_forward(app, index); break;
        case MESSAGE_ACTION_OPEN:
        case MESSAGE_ACTION_READ:  app->unknown_offer = 1; tui_app_activate_message(app, index); break;
        case MESSAGE_ACTION_RETRY:
            if (messaging_manager_retry_message(app->deps.messaging, m->id) == 0) tui_app_toast(app, "Retrying\xE2\x80\xA6", 0);
            break;
        case MESSAGE_ACTION_DELETE:
            tui_app_open_delete_menu(app, index, app->message_menu.anchor_y, app->message_menu.anchor_x);
            break;
        case MESSAGE_ACTION_SAVE: tui_app_save_message(app, index); break;
        case MESSAGE_ACTION_GOTO_QUOTE: tui_app_go_to_quote(app, index); break;
        case MESSAGE_ACTION_INFO: message_info_panel_open(&app->message_info, m, chat_jid_is_group(m->chat_jid)); break;
        case MESSAGE_ACTION_DELETE_FOR_ME:
        case MESSAGE_ACTION_DELETE_FOR_EVERYONE: {
            int everyone = message_menu_choice(&app->message_menu) == MESSAGE_ACTION_DELETE_FOR_EVERYONE;
            char id[64];
            str_copy(id, sizeof(id), m->id);
            if (everyone && app->editing_id[0] && strcmp(app->editing_id, id) == 0) tui_app_cancel_edit(app);
            if (messaging_manager_delete(app->deps.messaging, id, everyone) == 0) {
                tui_app_toast(app, everyone ? "\xF0\x9F\x97\x91 Deleted for everyone" : "\xF0\x9F\x97\x91 Deleted for you", 0);
            } else {
                tui_app_toast(app, everyone ? "This message can no longer be deleted for everyone" : "Deleted here; your phone was not reachable", 1);
            }
            app->message_view.selected = -1;
            break;
        }
        default: break;
    }
}

void tui_app_open_reactions(TuiApp *app, int index) {
    const Message *m = message_at(app, index);
    if (m) reaction_palette_open(&app->palette, m->id);
}

void tui_app_open_emoji(TuiApp *app, EmojiPickerPurpose purpose, const char *message_id, const char *query) {
    if (purpose == EMOJI_PICKER_FOR_INPUT && !messaging_manager_open_jid(app->deps.messaging)[0]) {
        tui_app_toast(app, "Open a chat first", 1);
        return;
    }
    emoji_picker_open(&app->emoji_picker, app->deps.emoji, purpose, message_id, settings(app)->recent_emoji, query);
}

/* Moves the emoji to the front of the recent list in the config file. */
static void remember_emoji(TuiApp *app, const char *glyph) {
    Settings s = *settings(app);
    char out[sizeof(s.recent_emoji)], buf[sizeof(s.recent_emoji)];
    str_copy(out, sizeof(out), glyph);
    str_copy(buf, sizeof(buf), s.recent_emoji);
    char *save = NULL;
    int kept = 1;
    for (char *t = strtok_r(buf, " ", &save); t && kept < EMOJI_PICKER_RECENT; t = strtok_r(NULL, " ", &save)) {
        if (strcmp(t, glyph) == 0) continue;
        if (strlen(out) + strlen(t) + 2 >= sizeof(out)) break;
        strcat(out, " ");
        strcat(out, t);
        kept++;
    }
    if (strcmp(out, s.recent_emoji) == 0) return;
    str_copy(s.recent_emoji, sizeof(s.recent_emoji), out);
    settings_manager_apply(app->deps.settings, &s);
}

void tui_app_emoji_chosen(TuiApp *app) {
    const char *glyph = emoji_picker_choice(&app->emoji_picker, app->deps.emoji);
    if (!glyph[0]) return;
    if (app->emoji_picker.purpose == EMOJI_PICKER_FOR_REACTION) {
        messaging_manager_react(app->deps.messaging, app->emoji_picker.message_id, glyph);
    } else {
        wchar_t wide[16];
        size_t n = mbstowcs(wide, glyph, 15);
        for (size_t i = 0; n != (size_t)-1 && i < n; i++) composer_view_insert(&app->composer, wide[i]);
        app->focus = TUI_FOCUS_COMPOSER;
    }
    remember_emoji(app, glyph);
    app->dirty = 1;
}

void tui_app_reaction_chosen(TuiApp *app) {
    if (reaction_palette_wants_more(&app->palette)) {
        tui_app_open_emoji(app, EMOJI_PICKER_FOR_REACTION, app->palette.message_id, "");
        return;
    }
    const char *emoji = reaction_palette_choice(&app->palette);
    messaging_manager_react(app->deps.messaging, app->palette.message_id, emoji);
    if (emoji[0]) remember_emoji(app, emoji);
}

void tui_app_send_composer(TuiApp *app) {
    char *text = composer_view_text(&app->composer);
    if (!text) return;
    char *body = str_trim(text);
    const char *jid = messaging_manager_open_jid(app->deps.messaging);
    char dropped[1024];
    if (body[0] == '/' && body[1] != '/' && !app->attachment[0]) {
        composer_view_clear(&app->composer);
        tui_commands_run(app, body);
        free(text);
        return;
    }
    if (app->editing_id[0]) {
        if (*body && messaging_manager_edit(app->deps.messaging, app->editing_id, body) == 0) tui_app_toast(app, "\xE2\x9C\x8F Edited", 0);
        else if (*body) tui_app_toast(app, "This message can no longer be edited", 1);
        app->editing_id[0] = '\0';
        free(text);
        composer_view_clear(&app->composer);
        return;
    }
    if (body[0] == '/' && body[1] == '/') body++;             /* "//text" sends "/text" */
    if (app->attachment[0]) {
        if (messaging_manager_send_file(app->deps.messaging, app->attachment, body) != 0) {
            tui_app_toast(app, "Could not send the file (missing, or larger than 100 MB)", 1);
        }
        app->attachment[0] = '\0';
    } else if (dropped_path_resolve(body, dropped, sizeof(dropped)) == 0) {
        /* A drop without bracketed paste arrives as typed text: send the file. */
        if (messaging_manager_send_file(app->deps.messaging, dropped, "") != 0) {
            tui_app_toast(app, "Could not send the file", 1);
        }
    } else if (*body && app->mention_pick_count > 0) {
        messaging_manager_send_text_mentioning(app->deps.messaging, body, app->reply.id[0] ? &app->reply : NULL,
                                               app->mention_picks, app->mention_pick_count);
    } else if (*body) {
        messaging_manager_send_text(app->deps.messaging, body, app->reply.id[0] ? &app->reply : NULL);
    }
    free(text);
    tui_app_cancel_reply(app);
    tui_app_forget_mentions(app);
    composer_view_clear(&app->composer);
    if (jid[0]) messaging_manager_save_draft(app->deps.messaging, jid, "");
    tui_app_show_latest(app);
}

void tui_app_toggle_recording(TuiApp *app) {
    MediaManager *media = app->deps.media;
    if (!messaging_manager_open_jid(app->deps.messaging)[0]) {
        tui_app_toast(app, "Open a chat before recording a voice note", 1);
        return;
    }
    if (!media_manager_is_recording(media)) {
        if (media_manager_start_recording(media) != 0) {
            tui_app_toast(app, "Could not start recording (needs ffmpeg and a microphone)", 1);
        } else {
            messaging_manager_set_typing(app->deps.messaging, TYPING_RECORDING);
        }
        return;
    }
    messaging_manager_set_typing(app->deps.messaging, TYPING_PAUSED);
    char path[512];
    int seconds = 0;
    if (media_manager_finish_recording(media, path, sizeof(path), &seconds) == 0) {
        messaging_manager_send_voice(app->deps.messaging, path, seconds);
        tui_app_show_latest(app);
    } else {
        tui_app_toast(app, "Recording was too short or empty", 1);
    }
}

/* ---- files -------------------------------------------------------------- */

void tui_app_open_file_picker(TuiApp *app, FilePickerPurpose purpose) {
    int for_chat = purpose == FILE_PICKER_FOR_ATTACHMENT || purpose == FILE_PICKER_FOR_TONE;
    if (for_chat && !messaging_manager_open_jid(app->deps.messaging)[0]) {
        tui_app_toast(app, "Open a chat first", 1);
        return;
    }
    app->picker_purpose = purpose;
    file_picker_open(&app->file_picker, app->last_attach_dir[0] ? app->last_attach_dir : settings(app)->attach_dir);
}

/* A picture copied to the clipboard becomes the attachment. */
void tui_app_paste_image(TuiApp *app) {
    if (!messaging_manager_open_jid(app->deps.messaging)[0]) { tui_app_toast(app, "Open a chat first", 1); return; }
    char path[1024];
    switch (app->deps.clipboard_image->save(app->deps.clipboard_image, settings(app)->media_dir, path, sizeof(path))) {
        case CLIPBOARD_IMAGE_SAVED:
            tui_app_attach(app, path);
            break;
        case CLIPBOARD_IMAGE_EMPTY:
            tui_app_toast(app, "There is no picture on the clipboard", 1);
            break;
        default:
            tui_app_toast(app, "Pasting pictures needs wl-clipboard (Wayland) or xclip (X11)", 1);
            break;
    }
}

void tui_app_open_attach_menu(TuiApp *app) {
    if (!messaging_manager_open_jid(app->deps.messaging)[0]) { tui_app_toast(app, "Open a chat first", 1); return; }
    attach_menu_open(&app->attach_menu, media_manager_camera_available(app->deps.media));
    app->dirty = 1;
}

void tui_app_open_camera(TuiApp *app) {
    if (!messaging_manager_open_jid(app->deps.messaging)[0]) { tui_app_toast(app, "Open a chat first", 1); return; }
    tui_app_open_camera_for(app, CAMERA_FOR_ATTACHMENT);
}

void tui_app_open_camera_for(TuiApp *app, CameraPurpose purpose) {
    if (!media_manager_camera_available(app->deps.media)) { tui_app_toast(app, "No camera found on this computer", 1); return; }
    if (media_manager_start_camera(app->deps.media) != 0) { tui_app_toast(app, "The camera could not be started", 1); return; }
    app->camera_purpose = purpose;
    camera_view_open(&app->camera_view);
    app->dirty = 1;
}

/* A photo or video from the camera, for what the camera was opened for. */
static void use_camera_result(TuiApp *app, const char *path, int video) {
    switch (app->camera_purpose) {
        case CAMERA_FOR_AVATAR:
            if (video) { tui_app_toast(app, "Your profile photo has to be a photo, not a video", 1); break; }
            tui_app_account_file(app, FILE_PICKER_FOR_AVATAR, path);
            break;
        case CAMERA_FOR_STATUS:
            status_composer_dialog_set_file(&app->status_composer, path, video ? STATUS_KIND_VIDEO : STATUS_KIND_PHOTO);
            break;
        default:
            tui_app_attach(app, path);
            tui_app_toast(app, video ? "Video attached; add a caption and press Enter"
                                     : "Photo attached; add a caption and press Enter", 0);
            break;
    }
}

void tui_app_apply_attach_choice(TuiApp *app) {
    switch (attach_menu_choice(&app->attach_menu)) {
        case ATTACH_CHOICE_PHOTO:
            tui_app_open_camera(app);
            break;
        case ATTACH_CHOICE_FILE:
            tui_app_open_file_picker(app, FILE_PICKER_FOR_ATTACHMENT);
            break;
        default:
            break;
    }
    app->dirty = 1;
}

static void close_camera(TuiApp *app) {
    media_manager_stop_camera(app->deps.media);
    camera_view_close(&app->camera_view);
    clearok(curscr, TRUE);                                  /* wipe the Sixel picture */
    sixel_overlay_invalidate(&app->sixel_overlay);
    app->dirty = 1;
}

void tui_app_camera_action(TuiApp *app, CameraViewAction action) {
    CameraView *v = &app->camera_view;
    char path[1024];
    switch (action) {
        case CAMERA_VIEW_SNAP:
            if (!v->frame.pixels) return;                   /* nothing to snap yet */
            if (media_manager_snap_photo(app->deps.media, path, sizeof(path)) != CAMERA_SAVED) {
                tui_app_toast(app, "The photo could not be saved", 1);
                return;
            }
            media_manager_stop_camera(app->deps.media);     /* the frame shown stays frozen for review */
            camera_view_review(v, path);
            break;
        case CAMERA_VIEW_START_VIDEO:
            if (!v->frame.pixels) return;                   /* the camera has not started yet */
            if (media_manager_start_video(app->deps.media) != 0) { tui_app_toast(app, "Could not start recording", 1); return; }
            camera_view_recording(v);
            break;
        case CAMERA_VIEW_STOP_VIDEO: {
            int seconds = 0;
            if (media_manager_finish_video(app->deps.media, path, sizeof(path), &seconds) != 0) {
                tui_app_toast(app, "The video could not be saved", 1);
                close_camera(app);
                return;
            }
            camera_view_review_video(v, path, seconds);     /* the last frame stays for review */
            break;
        }
        case CAMERA_VIEW_PLAY:
            media_manager_activate(app->deps.media, v->photo, MESSAGE_TYPE_VIDEO);
            break;
        case CAMERA_VIEW_RETAKE:
            unlink(v->photo);
            if (media_manager_start_camera(app->deps.media) != 0) { tui_app_toast(app, "The camera could not be started", 1); close_camera(app); return; }
            camera_view_live(v);
            break;
        case CAMERA_VIEW_USE: {
            int video = v->video;
            str_copy(path, sizeof(path), v->photo);
            close_camera(app);
            use_camera_result(app, path, video);
            break;
        }
        case CAMERA_VIEW_CANCEL:
            if (v->phase == CAMERA_VIEW_REVIEW) unlink(v->photo);
            close_camera(app);
            break;
        default:
            return;
    }
    app->dirty = 1;
}

/* New live frames; without one after a few seconds the camera has most likely been refused. */
static void follow_camera(TuiApp *app, int64_t now) {
    static int64_t opened_ms = 0;
    CameraView *v = &app->camera_view;
    if (!v->open) { opened_ms = 0; return; }
    if (!opened_ms) opened_ms = now;
    if (v->phase == CAMERA_VIEW_REVIEW) return;
    if (v->phase == CAMERA_VIEW_RECORDING) {
        int seconds = media_manager_video_seconds(app->deps.media);
        if (seconds != v->seconds) { v->seconds = seconds; app->dirty = 1; }
        if (media_manager_video_limit_reached(app->deps.media)) {
            tui_app_camera_action(app, CAMERA_VIEW_STOP_VIDEO);
            tui_app_toast(app, "Videos stop at 3 minutes", 0);
            return;
        }
    }
    RgbImage frame;
    if (media_manager_camera_frame(app->deps.media, &frame)) {
        camera_view_set_frame(v, &frame);
        app->dirty = 1;
    } else if (!v->frame.pixels && now - opened_ms > 8000) {
        close_camera(app);
        tui_app_toast(app, "The camera sent no picture (on a Mac, allow your terminal in Privacy & Security, Camera)", 1);
    }
}

void tui_app_attach(TuiApp *app, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) { tui_app_toast(app, "That file does not exist", 1); return; }
    if (st.st_size > 100LL * 1024 * 1024) {
        tui_app_toast(app, "That file is larger than 100 MB, the WhatsApp limit tawk uses", 1);
        return;
    }
    str_copy(app->attachment, sizeof(app->attachment), path);
    const char *slash = strrchr(path, '/');
    if (slash) {
        size_t n = (size_t)(slash - path);
        if (n == 0) n = 1;
        if (n < sizeof(app->last_attach_dir)) { memcpy(app->last_attach_dir, path, n); app->last_attach_dir[n] = '\0'; }
        /* Remembered across sessions. */
        if (strcmp(settings(app)->attach_dir, app->last_attach_dir) != 0) {
            Settings s = *settings(app);
            str_copy(s.attach_dir, sizeof(s.attach_dir), app->last_attach_dir);
            settings_manager_apply(app->deps.settings, &s);
        }
    }
    app->focus = TUI_FOCUS_COMPOSER;
    app->dirty = 1;
}

void tui_app_file_picked(TuiApp *app, const char *path) {
    if (app->picker_purpose == FILE_PICKER_FOR_TONE) {
        messaging_manager_set_tone(app->deps.messaging, app->tone_jid, path);
        tui_app_toast(app, "\xF0\x9F\x8E\xB5 Tone set for this chat", 0);
        return;
    }
    if (app->picker_purpose == FILE_PICKER_FOR_AVATAR || app->picker_purpose == FILE_PICKER_FOR_STATUS) {
        tui_app_account_file(app, app->picker_purpose, path);
        return;
    }
    tui_app_attach(app, path);
}

/* ---- chat options and themes -------------------------------------------- */

void tui_app_open_chat_options(TuiApp *app, const char *jid) {
    int count = 0;
    const Chat *chats = messaging_manager_chats(app->deps.messaging, &count);
    for (int i = 0; jid && i < count; i++) {
        if (strcmp(chats[i].jid, jid) == 0) { chat_options_menu_open(&app->options, &chats[i]); return; }
    }
}

/* Soft lock: the chat's conversation is blurred until shown again. */
void tui_app_toggle_soft_lock(TuiApp *app, const char *jid) {
    if (!jid || !jid[0]) { tui_app_toast(app, "Select or open a chat first", 1); return; }
    char copy[128];
    str_copy(copy, sizeof(copy), jid);
    int locked = messaging_manager_toggle_soft_lock(app->deps.messaging, copy);
    if (locked && strcmp(messaging_manager_open_jid(app->deps.messaging), copy) == 0) {
        app->message_view.selected = -1;
        if (app->viewer.open) image_viewer_close(&app->viewer);
    }
    tui_app_toast(app, locked ? "\xF0\x9F\x99\x88 Soft-locked" : "\xF0\x9F\x91\x80 Shown", 0);
    sixel_overlay_invalidate(&app->sixel_overlay);
    app->dirty = 1;
}

/* The chat a soft-lock key acts on: the one selected in the list, else the open one. */
void tui_app_toggle_soft_lock_here(TuiApp *app) {
    const char *jid = messaging_manager_open_jid(app->deps.messaging);
    if (app->focus == TUI_FOCUS_CHATS) {
        const char *selected = tui_app_take_selected(app);
        if (selected) jid = selected;
    }
    tui_app_toggle_soft_lock(app, jid);
}

void tui_app_apply_chat_option(TuiApp *app) {
    MessagingManager *mm = app->deps.messaging;
    const char *jid = app->options.jid;
    int64_t now = (int64_t)time(NULL);
    switch (chat_options_menu_choice(&app->options)) {
        case CHAT_OPTION_MUTE_8H:     messaging_manager_mute_until(mm, jid, now + 8 * 3600); break;
        case CHAT_OPTION_MUTE_WEEK:   messaging_manager_mute_until(mm, jid, now + 7 * 86400); break;
        case CHAT_OPTION_MUTE_ALWAYS: messaging_manager_mute_until(mm, jid, -1); break;
        case CHAT_OPTION_UNMUTE:      messaging_manager_mute_until(mm, jid, 0); break;
        case CHAT_OPTION_PIN:
        case CHAT_OPTION_UNPIN:       messaging_manager_toggle_pin(mm, jid); break;
        case CHAT_OPTION_ARCHIVE:     messaging_manager_set_archived(mm, jid, 1); tui_app_toast(app, "\xF0\x9F\x97\x84  Archived", 0); break;
        case CHAT_OPTION_UNARCHIVE:   messaging_manager_set_archived(mm, jid, 0); tui_app_toast(app, "Moved back to chats", 0); break;
        case CHAT_OPTION_THEME:       tui_app_open_theme_picker(app, jid); break;
        case CHAT_OPTION_TONE_CHOOSE:
            str_copy(app->tone_jid, sizeof(app->tone_jid), jid);
            if (strcmp(messaging_manager_open_jid(mm), jid) != 0) tui_app_open_chat(app, jid);
            tui_app_open_file_picker(app, FILE_PICKER_FOR_TONE);
            break;
        case CHAT_OPTION_TONE_NONE:    messaging_manager_set_tone(mm, jid, "none"); tui_app_toast(app, "No sound for this chat", 0); break;
        case CHAT_OPTION_TONE_DEFAULT: messaging_manager_set_tone(mm, jid, ""); tui_app_toast(app, "Default tone", 0); break;
        case CHAT_OPTION_SOFT_LOCK:
        case CHAT_OPTION_SOFT_UNLOCK: tui_app_toggle_soft_lock(app, jid); break;
        case CHAT_OPTION_INFO: tui_app_open_contact(app, jid); break;
        case CHAT_OPTION_DELETE_CHAT: {
            char question[200];
            snprintf(question, sizeof(question), "Delete the chat with %.120s?", app->options.title);
            confirm_dialog_open(&app->confirm, CONFIRM_DELETE_CHAT, jid, " \xE2\x9A\xA0 Delete chat ", question,
                                "\xE2\x9A\xA0 This is permanent. Every message, photo and file in this chat is "
                                "deleted from this computer, your phone and your other linked devices. "
                                "It cannot be undone.", "Delete permanently", 1);
            break;
        }
        case CHAT_OPTION_CLEAR_DRAFT:
            messaging_manager_save_draft(mm, jid, "");
            if (strcmp(messaging_manager_open_jid(mm), jid) == 0) composer_view_clear(&app->composer);
            break;
        default: break;
    }
    app->dirty = 1;
}

void tui_app_open_theme_picker(TuiApp *app, const char *jid) {
    int count = 0;
    const Chat *chats = messaging_manager_chats(app->deps.messaging, &count);
    for (int i = 0; i < count; i++) {
        if (strcmp(chats[i].jid, jid) != 0) continue;
        if (strcmp(messaging_manager_open_jid(app->deps.messaging), jid) != 0) tui_app_open_chat(app, jid);
        theme_picker_overlay_open(&app->theme_picker, settings_manager_themes(app->deps.settings), jid,
                                  chats[i].name, chats[i].theme);
        return;
    }
}

void tui_app_preview_chat_theme(TuiApp *app) {
    IThemeRepository *themes = settings_manager_themes(app->deps.settings);
    const Theme *t = theme_picker_overlay_selected(&app->theme_picker, themes);
    tui_palette_apply_conversation(t ? t : settings_manager_theme(app->deps.settings));
    app->dirty = 1;
}

void tui_app_finish_theme_picker(TuiApp *app, int keep) {
    if (keep) {
        IThemeRepository *themes = settings_manager_themes(app->deps.settings);
        const Theme *t = theme_picker_overlay_selected(&app->theme_picker, themes);
        messaging_manager_set_chat_theme(app->deps.messaging, app->theme_picker.jid, t ? t->id : "");
        tui_app_toast(app, t ? "\xF0\x9F\x8E\xA8 Chat theme applied" : "This chat uses the app theme", 0);
    }
    app->conversation_theme_valid = 0;
    sync_conversation_theme(app);
}

/* ---- search and help ------------------------------------------------------ */

void tui_app_open_search(TuiApp *app, const char *query) {
    search_overlay_open(&app->search, query);
    if (query && query[0]) tui_app_run_search(app);
}

void tui_app_run_search(TuiApp *app) {
    Message *results = NULL;
    int count = 0;
    if (app->search.query[0]) messaging_manager_search(app->deps.messaging, app->search.query, 100, &results, &count);
    search_overlay_set_results(&app->search, results, count);
}

void tui_app_choose_search_result(TuiApp *app) {
    const Message *hit = search_overlay_selected(&app->search);
    if (!hit) return;
    char chat[128], id[64];
    str_copy(chat, sizeof(chat), hit->chat_jid);
    str_copy(id, sizeof(id), hit->id);
    search_overlay_close(&app->search);
    tui_app_open_chat(app, chat);
    if (!tui_app_show_message(app, id)) tui_app_toast(app, "That message is older than tawk can load", 1);
}

void tui_app_show_latest(TuiApp *app) {
    messaging_manager_show_latest(app->deps.messaging);
    message_view_release(&app->message_view);
    message_view_scroll_to_latest(&app->message_view);
    app->dirty = 1;
}

int tui_app_load_older(TuiApp *app) {
    int count = 0;
    const Message *msgs = messaging_manager_messages(app->deps.messaging, &count);
    message_view_hold(&app->message_view, msgs, count);
    if (messaging_manager_load_older(app->deps.messaging)) { app->dirty = 1; return 1; }
    message_view_release(&app->message_view);
    return 0;
}

/* Keeps a margin of messages loaded either side of the ones on screen, so
 * memory stays flat however far the user scrolls. Runs after a frame, when
 * the view knows what it has just drawn. */
static void keep_message_window(TuiApp *app) {
    MessageView *v = &app->message_view;
    int first = -1, last = -1, count = 0;
    if (!v->drawn) return;
    v->drawn = 0;
    if (!message_view_visible_range(v, &first, &last)) return;
    const Message *msgs = messaging_manager_messages(app->deps.messaging, &count);
    message_view_hold(v, msgs, count);
    if (messaging_manager_focus_window(app->deps.messaging, first, last)) app->dirty = 1;
    else message_view_release(v);
}

/* Selects a message of the open chat and scrolls to it, loading older pages
 * until it is in view (within reason). Returns 1 when found. */
int tui_app_show_message(TuiApp *app, const char *id) {
    message_view_release(&app->message_view);
    for (int attempt = 0; attempt < 80; attempt++) {
        int count = 0;
        const Message *msgs = messaging_manager_messages(app->deps.messaging, &count);
        for (int i = 0; i < count; i++) {
            if (strcmp(msgs[i].id, id) != 0) continue;
            app->message_view.selected = i;
            app->message_view.follow_selection = 1;
            app->focus = TUI_FOCUS_MESSAGES;
            app->dirty = 1;
            return 1;
        }
        if (!messaging_manager_load_older(app->deps.messaging)) break;
    }
    return 0;
}

/* Jumps from a reply to the message it quotes. */
void tui_app_go_to_quote(TuiApp *app, int index) {
    const Message *m = message_at(app, index);
    if (!m || !m->quoted_id[0]) return;
    char id[64];
    str_copy(id, sizeof(id), m->quoted_id);
    if (!tui_app_show_message(app, id)) {
        tui_app_toast(app, messaging_manager_history_pending(app->deps.messaging)
                               ? "Asking your phone for older messages; try again in a moment"
                               : "The quoted message is not on this computer", 1);
    }
}

void tui_app_open_help(TuiApp *app) {
    int count = 0;
    const SlashCommand *all = tui_commands_all(&count);
    size_t cap = 8192, used = 0;
    char *text = malloc(cap);
    if (!text) return;
    used += (size_t)snprintf(text + used, cap - used, "Commands (type them in the input line)\n\n");
    for (int i = 0; i < count && used < cap; i++) {
        used += (size_t)snprintf(text + used, cap - used, "  /%-12s %-22s %s\n", all[i].name, all[i].args, all[i].help);
    }
    if (used < cap) {
        snprintf(text + used, cap - used, "%s",
                 "\nKeys\n\n"
                 "  Tab / Shift+Tab     move between chats, messages and the input\n"
                 "  Ctrl+K              search every chat\n"
                 "  Ctrl+F or /         search the chat list (or just type in it)\n"
                 "  Ctrl+N              next unread chat\n"
                 "  Ctrl+B              show or hide the chat list\n"
                 "  Ctrl+R              record a voice note\n"
                 "  Ctrl+O              attach a file\n"
                 "  Ctrl+D              do not disturb\n"
                 "  Ctrl+L              start the screensaver\n"
                 "  Ctrl+E              emoji\n"
                 "  Alt+V               attach the picture on the clipboard\n"
                 "  F2                  settings\n"
                 "  Ctrl+Q              quit\n\n"
                 "  In the chat list:   type to search  Alt+O options  Alt+M mute  Alt+P pin  Alt+A archive\n"
                 "  On a message:       Alt+Q reply  Alt+E react  Alt+Shift+E edit  Alt+M menu  Alt+R retry\n"
                 "                      Enter opens photos, videos and long messages; typing writes in the input\n"
                 "  In the input:       Enter send  Alt+Enter new line  \xE2\x86\x91\xE2\x86\x93 move between lines\n"
                 "                      \xE2\x86\x91 when empty edits your last message, if it is the newest\n");
    }
    text_reader_open(&app->reader, "Help", text);
    free(text);
}

/* ---- settings panel host ------------------------------------------------ */

static const Settings *host_settings(void *ctx) { return settings((TuiApp *)ctx); }

static int host_apply(void *ctx, const Settings *updated) {
    tui_app_apply_settings(ctx, updated);
    return 0;
}

static IThemeRepository *host_themes(void *ctx) { return settings_manager_themes(((TuiApp *)ctx)->deps.settings); }

static void host_preview(void *ctx, const Theme *theme) {
    TuiApp *app = ctx;
    tui_palette_apply(theme);
    app->conversation_theme_valid = 0;
}

static void host_action(void *ctx, MenuAction action) {
    TuiApp *app = ctx;
    switch (action) {
        case MENU_ACTION_SELF_APPROVAL_CHATS:
            tui_app_open_self_chats(app);
            break;
        case MENU_ACTION_LOGOUT:
            messaging_manager_logout(app->deps.messaging);
            settings_panel_close(&app->settings_panel);
            tui_app_toast(app, "Logged out; link again with a QR code or phone number", 0);
            break;
        case MENU_ACTION_TEST_SOUND:
            if (app->deps.sound_player->play(app->deps.sound_player, settings(app)->sound_file) != 0) {
                tui_app_toast(app, "No audio player found for this audio system", 1);
            }
            break;
        case MENU_ACTION_TEST_NOTIFICATION: {
            Notification n;
            memset(&n, 0, sizeof(n));
            str_copy(n.title, sizeof(n.title), APP_NAME);
            str_copy(n.body, sizeof(n.body), "This is what a new message looks like");
            n.type = MESSAGE_TYPE_TEXT;
            app->deps.notifier->notify(app->deps.notifier, &n);
            break;
        }
        case MENU_ACTION_RUN_SCREENSAVER:
            settings_panel_close(&app->settings_panel);
            app->run_screensaver_now = 1;
            break;
        case MENU_ACTION_RELOAD_THEMES: {
            IThemeRepository *repo = host_themes(app);
            char msg[64];
            snprintf(msg, sizeof(msg), "Loaded %d themes", repo->reload(repo));
            tui_palette_apply(settings_manager_theme(app->deps.settings));
            app->conversation_theme_valid = 0;
            tui_app_toast(app, msg, 0);
            break;
        }
        case MENU_ACTION_RETRY_CONNECTION:
            messaging_manager_retry_now(app->deps.messaging);
            tui_app_toast(app, "Reconnecting\xE2\x80\xA6", 0);
            break;
        case MENU_ACTION_CLEAR_LOGS:
            confirm_dialog_open(&app->confirm, CONFIRM_CLEAR_LOGS, "", " Clear logs ", "Clear the log files?",
                                "Empties tawk.log and the backend's logs in ~/.local/state/tawk. "
                                "They only help with troubleshooting.", "Clear logs", 0);
            break;
        default:
            break;
    }
}

/* ---- contact details ---------------------------------------------------- */

static const Chat *chat_by_jid(TuiApp *app, const char *jid) {
    int count = 0;
    const Chat *chats = messaging_manager_chats(app->deps.messaging, &count);
    for (int i = 0; i < count; i++) if (strcmp(chats[i].jid, jid) == 0) return &chats[i];
    return NULL;
}

const char *tui_app_member_name(void *ctx, const char *jid) {
    static char name[128];
    TuiApp *app = ctx;
    if (strcmp(jid, messaging_manager_user_jid(app->deps.messaging)) == 0) return "You";
    messaging_manager_display_name(app->deps.messaging, jid, name, sizeof(name));
    return name;
}

void tui_app_open_contact(TuiApp *app, const char *jid) {
    const Chat *chat = jid ? chat_by_jid(app, jid) : NULL;
    if (!chat) { tui_app_toast(app, "Select or open a chat first", 1); return; }
    ContactProfile fresh;
    profile_manager_details(app->deps.profiles, chat->jid, 1, &fresh);      /* ask WhatsApp again */
    contact_profile_dispose(&fresh);
    contact_panel_open(&app->contact, chat, profile_manager_is_blocked(app->deps.profiles, chat->jid));
    app->dirty = 1;
}

void tui_app_show_portrait(TuiApp *app, const char *jid) {
    const Chat *chat = chat_by_jid(app, jid);
    if (!chat || chat->soft_locked) return;
    const char *full = profile_manager_full_picture(app->deps.profiles, jid);
    const char *preview = profile_manager_picture(app->deps.profiles, jid);
    if (!full && !preview) { tui_app_toast(app, "No profile picture", 1); return; }
    image_viewer_open_portrait(&app->viewer, jid, chat->name, full ? full : preview);
    app->dirty = 1;
}

static void export_chat(TuiApp *app, const char *jid, int with_media) {
    char dir[512], out[1100];
    if (settings(app)->download_dir[0]) path_expand_home(settings(app)->download_dir, dir, sizeof(dir));
    else path_download_dir(dir, sizeof(dir));
    if (messaging_manager_export_chat(app->deps.messaging, jid, dir, with_media, out, sizeof(out)) == 0) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "\xF0\x9F\x93\xA4 Exported to %s", out);
        tui_app_toast(app, msg, 0);
    } else {
        tui_app_toast(app, "Could not export the chat (check Settings, Media, Save folder)", 1);
    }
}

void tui_app_contact_action(TuiApp *app) {
    char jid[128], name[128], question[200];
    str_copy(jid, sizeof(jid), app->contact.jid);
    str_copy(name, sizeof(name), app->contact.name);
    switch (contact_panel_choice(&app->contact)) {
        case CONTACT_ACTION_VIEW_PHOTO: tui_app_show_portrait(app, jid); break;
        case CONTACT_ACTION_SEARCH:     app->contact.open = 0; tui_app_open_search(app, ""); break;
        case CONTACT_ACTION_OPTIONS:    app->contact.open = 0; tui_app_open_chat_options(app, jid); break;
        case CONTACT_ACTION_SOFT_LOCK:  tui_app_toggle_soft_lock(app, jid); break;
        case CONTACT_ACTION_EXPORT:     export_chat(app, jid, 0); break;
        case CONTACT_ACTION_EXPORT_MEDIA: export_chat(app, jid, 1); break;
        case CONTACT_ACTION_UNBLOCK:
            profile_manager_set_blocked(app->deps.profiles, jid, 0);
            contact_panel_open(&app->contact, chat_by_jid(app, jid), 0);
            tui_app_toast(app, "Unblocked", 0);
            break;
        case CONTACT_ACTION_BLOCK:
            snprintf(question, sizeof(question), "Block %.120s?", name);
            confirm_dialog_open(&app->confirm, CONFIRM_BLOCK, jid, " Block contact ", question,
                                "They will not be able to call you or send you messages, on any of your devices. "
                                "You can unblock them later.", "Block", 0);
            break;
        case CONTACT_ACTION_CLEAR:
            snprintf(question, sizeof(question), "Clear the chat with %.120s?", name);
            confirm_dialog_open(&app->confirm, CONFIRM_CLEAR_CHAT, jid, " \xE2\x9A\xA0 Clear chat ", question,
                                "\xE2\x9A\xA0 This permanently deletes every message of this chat from this computer. "
                                "Downloaded files stay in the media folder. The chat stays in the list, "
                                "and your phone keeps its copy.",
                                "Clear permanently", 1);
            break;
        case CONTACT_ACTION_DELETE:
            snprintf(question, sizeof(question), "Delete the chat with %.120s?", name);
            confirm_dialog_open(&app->confirm, CONFIRM_DELETE_CHAT, jid, " \xE2\x9A\xA0 Delete chat ", question,
                                "\xE2\x9A\xA0 This is permanent. Every message, photo and file in this chat is "
                                "deleted from this computer, your phone and your other linked devices. "
                                "It cannot be undone.", "Delete permanently", 1);
            break;
        default: break;
    }
    app->dirty = 1;
}

/* Decline or put away the ringing call. */
void tui_app_call_choice(TuiApp *app, IncomingCallChoice choice) {
    if (choice == INCOMING_CALL_DECLINE) {
        if (call_manager_decline(app->deps.calls) == 0) tui_app_toast(app, "\xF0\x9F\x93\xB5 Call declined", 0);
    } else if (choice == INCOMING_CALL_DISMISS) {
        call_manager_dismiss(app->deps.calls);
    }
    app->dirty = 1;
}

/* While a call rings: the ringtone every few seconds and the flashing title (unless do not disturb). */
static void ring(TuiApp *app, int64_t now) {
    const IncomingCall *call = call_manager_ringing(app->deps.calls);
    if (!call || settings(app)->do_not_disturb) return;
    if (now - app->last_ring_ms >= 3000) {
        app->last_ring_ms = now;
        if (settings(app)->sound) app->deps.sound_player->play(app->deps.sound_player, settings(app)->sound_file);
        title_flasher_start(app->deps.title, now);
    }
}

/* ✕ on the message input: asks before throwing away what is typed. */
void tui_app_ask_clear_input(TuiApp *app) {
    if (composer_view_is_empty(&app->composer)) return;
    confirm_dialog_open(&app->confirm, CONFIRM_CLEAR_INPUT, "", " Clear message ", "Clear what you typed?",
                        "The text in the message box will be removed. It has not been sent.", "Clear", 0);
    app->dirty = 1;
}

/* The user said yes to the open confirmation. */
void tui_app_confirmed(TuiApp *app) {
    switch (app->confirm.purpose) {
        case CONFIRM_BLOCK:
            profile_manager_set_blocked(app->deps.profiles, app->confirm.subject, 1);
            if (app->contact.open) contact_panel_open(&app->contact, chat_by_jid(app, app->confirm.subject), 1);
            tui_app_toast(app, "\xF0\x9F\x9A\xAB Blocked", 0);
            break;
        case CONFIRM_CLEAR_CHAT:
            messaging_manager_clear_chat(app->deps.messaging, app->confirm.subject);
            tui_app_toast(app, "\xF0\x9F\xA7\xB9 Chat cleared", 0);
            break;
        case CONFIRM_DELETE_CHAT: {
            char jid[128];
            str_copy(jid, sizeof(jid), app->confirm.subject);
            if (strcmp(messaging_manager_open_jid(app->deps.messaging), jid) == 0) {
                if (app->viewer.open) image_viewer_close(&app->viewer);
                app->message_view.selected = -1;
                composer_view_clear(&app->composer);
            }
            app->contact.open = 0;
            if (messaging_manager_delete_chat(app->deps.messaging, jid) == 0) tui_app_toast(app, "\xF0\x9F\x97\x91 Chat deleted", 0);
            else tui_app_toast(app, "Chat deleted here; your phone was not reachable", 1);
            break;
        }
        case CONFIRM_REMOVE_PHOTO:
            tui_app_remove_profile_photo(app);
            break;
        case CONFIRM_CLEAR_INPUT:
            composer_view_clear(&app->composer);
            emoji_suggestions_close(&app->emoji_suggestions);
            break;
        case CONFIRM_USE_WHATSMEOW:
            tui_app_switch_to_whatsmeow(app);
            break;
        case CONFIRM_ENABLE_AGENTS:
            apply_settings_now(app, &app->pending_settings);
            tui_app_toast(app, "\xF0\x9F\xA4\x96 Agents can reach tawk now; the Agentic tab (F3) shows them", 0);
            break;
        case CONFIRM_CLEAR_LOGS: {
            long long freed = log_clear();
            char msg[96];
            if (freed < 0) snprintf(msg, sizeof(msg), "There is no log file to clear");
            else if (freed >= 1024 * 1024) snprintf(msg, sizeof(msg), "\xF0\x9F\xA7\xB9 Logs cleared (%.1f MB freed)", (double)freed / (1024.0 * 1024.0));
            else snprintf(msg, sizeof(msg), "\xF0\x9F\xA7\xB9 Logs cleared (%lld KB freed)", (freed + 1023) / 1024);
            tui_app_toast(app, msg, freed < 0);
            break;
        }
        default:
            break;
    }
    app->dirty = 1;
}

static const char *connection_text(AuthState auth) {
    switch (auth) {
        case AUTH_STATE_CONNECTED:    return "online";
        case AUTH_STATE_RECONNECTING: return "reconnecting";
        case AUTH_STATE_NEEDS_LOGIN:  return "not linked";
        case AUTH_STATE_FAILED:       return "offline";
        default:                      return "connecting";
    }
}

static void host_info(void *ctx, MenuInfo info, char *out, size_t size) {
    TuiApp *app = ctx;
    MessagingManager *mm = app->deps.messaging;
    switch (info) {
        case MENU_INFO_NAME:        str_copy(out, size, tui_app_user_name(app)[0] ? tui_app_user_name(app) : "-"); break;
        case MENU_INFO_NUMBER:
            if (messaging_manager_user_jid(mm)[0]) contact_phone_from_jid(messaging_manager_user_jid(mm), out, size);
            else str_copy(out, size, "-");
            break;
        case MENU_INFO_CONNECTION:  str_copy(out, size, connection_text(messaging_manager_auth_state(mm))); break;
        case MENU_INFO_BACKEND:     str_copy(out, size, app->deps.backend_name); break;
        case MENU_INFO_AUDIO:       str_copy(out, size, app->deps.audio_backend_name); break;
        case MENU_INFO_CONFIG_PATH: str_copy(out, size, settings(app)->config_path); break;
        case MENU_INFO_USER_THEMES: str_copy(out, size, app->deps.user_theme_dir); break;
        case MENU_INFO_VERSION:     snprintf(out, size, "%s %s", APP_NAME, APP_VERSION); break;
        case MENU_INFO_AUTHOR:      snprintf(out, size, "%s <%s>", APP_AUTHOR, APP_AUTHOR_EMAIL); break;
        case MENU_INFO_AGENTS: {
            const AutomationStatus *st = app->deps.automation ? automation_manager_status(app->deps.automation) : NULL;
            if (!settings(app)->control_socket) str_copy(out, size, "off");
            else if (st && st->error[0]) str_copy(out, size, st->error);
            else if (st && st->listening) snprintf(out, size, "listening, %d connected", st->session_count);
            else str_copy(out, size, "starting");
            break;
        }
        case MENU_INFO_SELF_CHATS:  tui_app_self_chats_summary(app, out, size); break;
        default:                    out[0] = '\0'; break;
    }
}

/* ---- screensaver and presence ------------------------------------------- */

static int screensaver_wake(void *user) {
    TuiApp *app = user;
    ManagerChanges ch;
    messaging_manager_tick(app->deps.messaging, &ch);
    return settings(app)->wake_on_message && ch.notified > 0;
}

static void set_active(TuiApp *app, int active) {
    if (active == app->active) return;
    app->active = active;
    messaging_manager_set_active(app->deps.messaging, active);
    tui_app_accounts_set_active(app, active);
}

static void run_screensaver(TuiApp *app) {
    const Settings *s = settings(app);
    if (!s->screensaver_command[0]) { tui_app_toast(app, "No screensaver command is set (Settings, Screensaver)", 1); return; }
    if (!s->config_trusted) { tui_app_toast(app, "The config file is writable by others; the screensaver command is disabled", 1); return; }
    set_active(app, 0);
    def_prog_mode();
    terminal_modes(0, 0);
    endwin();
    if (s->mouse) {                                   /* a click reaches the screensaver as input, and wakes tawk */
        fputs("\033[?1000h", stdout);
        fflush(stdout);
    }
    app->deps.screensaver->run(app->deps.screensaver, s->screensaver_command, screensaver_wake, app);
    reset_prog_mode();
    terminal_modes(1, s->mouse);
    clearok(curscr, TRUE);
    sixel_overlay_invalidate(&app->sixel_overlay);
    app->caret_visible = -1;
    app->dirty = 1;
    idle_tracker_reset(&app->idle);
    set_active(app, 1);
}

void tui_app_start_screensaver(TuiApp *app) { app->run_screensaver_now = 1; }

static void maybe_screensaver(TuiApp *app) {
    const Settings *s = settings(app);
    if (app->run_screensaver_now) {
        app->run_screensaver_now = 0;
        run_screensaver(app);
        return;
    }
    if (!s->screensaver || media_manager_is_recording(app->deps.media) || tui_app_show_login(app)) return;
    if (idle_tracker_is_idle(&app->idle, s->idle_minutes)) run_screensaver(app);
}

/* ---- per-tick updates --------------------------------------------------- */

static void apply_changes(TuiApp *app, const ManagerChanges *ch) {
    /* A photo downloaded for the viewer just sharpens there; anything else
     * the user asked to open opens now. */
    const char *viewer = settings(app)->image_viewer;
    int builtin = !viewer[0] || strcmp(viewer, "builtin") == 0;
    size_t plen = strlen(ch->media_path);
    int is_pdf = ch->media_type == MESSAGE_TYPE_DOCUMENT && plen > 4 && strcasecmp(ch->media_path + plen - 4, ".pdf") == 0;
    int for_viewer = builtin && (ch->media_type == MESSAGE_TYPE_IMAGE || is_pdf) && strcmp(ch->media_id, app->open_outside_id) != 0;
    if (ch->media_id[0] && strcmp(ch->media_id, app->save_after_id) == 0) {
        app->save_after_id[0] = '\0';
        Message saved;
        if (messaging_manager_get(app->deps.messaging, ch->media_id, &saved) == 0) {
            save_file(app, &saved);
            message_dispose(&saved);
        }
        for_viewer = 1;                                   /* saved, not opened */
    }
    if (ch->media_id[0] && strcmp(ch->media_id, app->open_outside_id) == 0) app->open_outside_id[0] = '\0';
    if (ch->media_id[0] && !for_viewer) {
        if (media_manager_activate(app->deps.media, ch->media_path, ch->media_type) != 0) {
            tui_app_toast(app, "Downloaded, but no viewer or player is available", 1);
        }
    }
    if (ch->error[0]) tui_app_toast(app, ch->error, 1);
    if (ch->chats) sync_conversation_theme(app);
}

/* Someone typing in a chat that is not open ("Jan is typing" or "Jan is
 * typing in Dev team"), leaving out soft-locked chats. */
static int typing_elsewhere(TuiApp *app, char *out, size_t size) {
    int count = 0;
    const Chat *chats = messaging_manager_chats(app->deps.messaging, &count);
    for (int i = 0; i < count; i++) {
        const Chat *c = &chats[i];
        if (!c->typing[0] || c->soft_locked) continue;
        if (strncmp(c->typing, "typing", 6) == 0) snprintf(out, size, "%s is typing", c->name);
        else if (strncmp(c->typing, "recording", 9) == 0) snprintf(out, size, "%s is recording audio", c->name);
        else {
            /* "Dev team: Jan is typing", so the animated dots follow the typing. */
            char who[64];
            str_copy(who, sizeof(who), c->typing);
            size_t n = strlen(who);
            if (n >= 3 && strcmp(who + n - 3, "\xE2\x80\xA6") == 0) who[n - 3] = '\0';
            snprintf(out, size, "%s: %s", c->name, who);
        }
        return 1;
    }
    return 0;
}

static void update_tab(TuiApp *app, int64_t now) {
    MessagingManager *mm = app->deps.messaging;
    ConnectionHealth health;
    messaging_manager_health(mm, &health);
    AuthState auth = messaging_manager_auth_state(mm);
    /* What is happening now, for the title; a soft-locked chat stays anonymous. */
    const Chat *chat = messaging_manager_open_chat_info(mm);
    int hidden = chat && chat->soft_locked;
    char activity[256] = "";
    int busy = 0, typing = 0;
    if (media_manager_is_recording(app->deps.media)) {
        int secs = media_manager_recording_seconds(app->deps.media);
        snprintf(activity, sizeof(activity), "Recording %d:%02d", secs / 60, secs % 60);
    } else if (auth == AUTH_STATE_STARTING || auth == AUTH_STATE_RECONNECTING) {
        snprintf(activity, sizeof(activity), "%s", auth == AUTH_STATE_STARTING ? "Connecting\xE2\x80\xA6" : "Reconnecting\xE2\x80\xA6");
        busy = 1;
    } else if (messaging_manager_history_pending(mm)) {
        snprintf(activity, sizeof(activity), "Loading older messages");
        busy = 1;
    } else if (chat && !hidden && chat->typing[0]) {
        snprintf(activity, sizeof(activity), "%s", chat->typing);
        typing = 1;
    } else if (typing_elsewhere(app, activity, sizeof(activity))) {
        typing = 1;
    } else if (app->deps.video_posters->pending(app->deps.video_posters) ||
               app->deps.document_pages->pending(app->deps.document_pages)) {
        snprintf(activity, sizeof(activity), "Preparing previews");
        busy = 1;
    }
    TabStatus st = {
        .chat_name = chat ? (hidden ? "\xF0\x9F\x99\x88" : chat->name) : "",
        .activity = activity,
        .busy = busy,
        .typing = typing,
        .user_name = tui_app_user_name(app),
        .connection = auth == AUTH_STATE_CONNECTED ? 0 : auth == AUTH_STATE_NEEDS_LOGIN ? 3 :
                      (health.breaker == CIRCUIT_OPEN || !health.will_retry) && health.show_overlay ? 2 : 1,
        .dnd = settings(app)->do_not_disturb,
        .recording = media_manager_is_recording(app->deps.media),
        .playing = media_manager_playing(app->deps.media)[0] != '\0',
        .tally = messaging_manager_tally(mm),
        .progress = health.breaker == CIRCUIT_OPEN ? TERMINAL_PROGRESS_ERROR :
                    (auth == AUTH_STATE_RECONNECTING || auth == AUTH_STATE_STARTING) ? TERMINAL_PROGRESS_BUSY :
                    TERMINAL_PROGRESS_NONE,
    };
    title_flasher_tick(app->deps.title, now, &st, settings(app));
}

static int someone_typing(TuiApp *app) {
    const Chat *c = messaging_manager_open_chat_info(app->deps.messaging);
    return c && c->typing[0] && !c->soft_locked;
}

/* Animations that need regular frames: blink, recording counter, title flash,
 * outage countdown. Everything else redraws only when something changed. */
static int animating(TuiApp *app, int64_t now) {
    ConnectionHealth health;
    messaging_manager_health(app->deps.messaging, &health);
    return blink_state_on(app->deps.blink, NULL, now) || app->deps.blink->until_ms > now ||
           media_manager_is_recording(app->deps.media) || health.show_overlay ||
           app->deps.title->flashing_since_ms != 0 || app->toast[0] ||
           messaging_manager_history_pending(app->deps.messaging) ||
           app->deps.video_posters->pending(app->deps.video_posters) ||   /* show a video frame once ready */
           app->deps.document_pages->pending(app->deps.document_pages) ||
           messaging_manager_auth_state(app->deps.messaging) == AUTH_STATE_STARTING ||   /* the title spinner */
           messaging_manager_auth_state(app->deps.messaging) == AUTH_STATE_RECONNECTING ||
           (app->confirm.open && app->confirm.danger) ||                   /* the flashing warning */
           app->status_composer.busy ||                                    /* waiting to hear the status went out */
           status_feed_dialogs_viewing(&app->feed) ||                      /* the status progress bar filling */
           call_manager_ringing(app->deps.calls) != NULL ||                /* the pulsing call box */
           someone_typing(app) ||                                          /* the typing dots */
           media_manager_playing(app->deps.media)[0] != '\0';             /* the voice note progress */
}

static int changes_any(const ManagerChanges *ch) {
    return ch->chats || ch->messages || ch->profiles || ch->auth || ch->connection || ch->notified ||
           ch->statuses || ch->media_id[0] || ch->error[0];
}

/* ---- splash ------------------------------------------------------------- */

/* A frame of the start-up animation; any key ends it (and is not passed on). */
static void run_splash_frame(TuiApp *app, int64_t now) {
    char detail[64];
    snprintf(detail, sizeof(detail), "v%s", APP_VERSION);
    AuthState auth = messaging_manager_auth_state(app->deps.messaging);
    const char *status = auth == AUTH_STATE_CONNECTED ? "Connected" : auth == AUTH_STATE_NEEDS_LOGIN ? "Ready to link your phone"
                                                                    : "Connecting to WhatsApp\xE2\x80\xA6";
    splash_view_render(&app->splash, (UiRect){ 0, 0, LINES, COLS }, now, detail, status);
    refresh();
    wint_t wc;
    wtimeout(stdscr, 33);
    int rc = wget_wch(stdscr, &wc);
    if (rc == ERR) return;
    splash_view_skip(&app->splash);
    wtimeout(stdscr, 0);
    while (wget_wch(stdscr, &wc) != ERR) {}                 /* the rest of a key sequence or a mouse report */
}

/* ---- message text as shown ------------------------------------------------ */

static int format_through_manager(void *ctx, const Message *m, StyledText *out) {
    return messaging_manager_format_message(((TuiApp *)ctx)->deps.messaging, m, out);
}

static void preview_through_manager(void *ctx, const Message *m, char *out, size_t size) {
    messaging_manager_message_preview(((TuiApp *)ctx)->deps.messaging, m, out, size);
}

static int status_through_feed(void *ctx, const char *id, StatusUpdate *out) {
    TuiApp *app = ctx;
    return app->deps.feed ? status_feed_manager_get(app->deps.feed, id, out) : -1;
}

/* ---- lifecycle ---------------------------------------------------------- */

TuiApp *tui_app_create(const TuiAppDeps *deps) {
    TuiApp *app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->deps = *deps;
    chat_list_view_init(&app->chat_list);
    app->chat_list.pinned_collapsed = settings(app)->pinned_folded;      /* folding as it was left */
    app->chat_list.others_collapsed = settings(app)->chats_folded;
    message_view_init(&app->message_view);
    composer_view_init(&app->composer);
    login_view_init(&app->login);
    SettingsPanelHost host = { app, host_settings, host_apply, host_themes, host_preview, host_action, host_info };
    settings_panel_init(&app->settings_panel, host);
    file_picker_init(&app->file_picker);
    search_overlay_init(&app->search);
    app->thumbs = thumbnail_cache_create(512);   /* more than a full window of pictures, so scrolling never decodes one twice */
    app->sixels = sixel_image_cache_create(24);
    app->media_sources = (MediaSources){ app->deps.video_posters, app->deps.document_pages };
    app->portraits = (PortraitSource){ app, portrait_of };
    app->formatter = (MessageFormatter){ app, format_through_manager, preview_through_manager };
    app->status_source = (StatusSource){ app, status_through_feed };
    app->focus = TUI_FOCUS_CHATS;
    app->last_auth = AUTH_STATE_STARTING;
    app->caret_visible = -1;
    app->restore_pending = settings(app)->reopen_last_chat && settings(app)->last_chat[0];
    app->dirty = 1;
    app->active = 1;
    idle_tracker_reset(&app->idle);
    return app;
}

void tui_app_destroy(TuiApp *app) {
    if (!app) return;
    unified_chat_list_free(&app->chat_rows);
    message_view_dispose(&app->message_view);
    file_picker_dispose(&app->file_picker);
    search_overlay_close(&app->search);
    text_reader_close(&app->reader);
    thumbnail_cache_destroy(app->thumbs);
    sixel_image_cache_destroy(app->sixels);
    free(app);
}

/* Waits up to 100 ms for the first key (returning the moment one arrives),
 * then handles every key already queued before the next frame is drawn, so a
 * burst of typing or key repeat costs one redraw instead of one per key. */
#define MAX_KEYS_PER_FRAME 256

/* How long to wait for a key: briefly while the camera picture is live, so
 * each new frame is drawn as soon as it arrives. */
static int input_wait_ms(const TuiApp *app) {
    return app->camera_view.open && app->camera_view.phase != CAMERA_VIEW_REVIEW ? 10 : 100;
}

static void read_input(TuiApp *app) {
    wint_t wc;
    wtimeout(stdscr, input_wait_ms(app));
    int rc = wget_wch(stdscr, &wc);
    if (rc == ERR) return;
    app->dirty = 1;
    app->last_key_ms = clock_now_ms();
    idle_tracker_reset(&app->idle);
    title_flasher_acknowledge(app->deps.title);
    set_active(app, 1);
    for (int n = 0; rc != ERR && n < MAX_KEYS_PER_FRAME && app->running; n++) {
        tui_input_dispatch(app, rc == KEY_CODE_YES, (int)wc);
        wtimeout(stdscr, 0);
        rc = wget_wch(stdscr, &wc);
        wtimeout(stdscr, input_wait_ms(app));
    }
}

int tui_app_run(TuiApp *app) {
    srandom((unsigned)time(NULL) ^ (unsigned)getpid());     /* varied status colours */
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    set_escdelay(25);
    wtimeout(stdscr, 100);
    tui_palette_init();
    tui_palette_apply(settings_manager_theme(app->deps.settings));
    const Settings *s = settings(app);
    if (s->mouse) {
        mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
        mouseinterval(0);
    }
    terminal_modes(1, s->mouse);
    str_copy(app->followed_theme, sizeof(app->followed_theme), s->theme);
    app->followed_mouse = s->mouse;
    app->settings_revision = settings_manager_revision(app->deps.settings);

    tui_app_accounts_start(app);
    if (s->splash) splash_view_start(&app->splash, clock_now_ms());
    app->running = 1;
    while (app->running && !*app->deps.quit_requested) {
        int64_t now = clock_now_ms();
        ManagerChanges ch;
        messaging_manager_tick(app->deps.messaging, &ch);
        profile_manager_tick(app->deps.profiles);
        tui_app_account_tick(app);
        tui_app_statuses_tick(app);
        tui_app_scheduling_tick(app);
        tui_app_agents_tick(app);
        tui_app_accounts_tick(app);
        if (ch.chats || ch.messages) app->chat_rows_stale = 1;
        ring(app, now);
        apply_changes(app, &ch);
        follow_auth(app, now);
        restore_last_chat(app);
        if (media_manager_recording_limit_reached(app->deps.media)) {
            tui_app_toggle_recording(app);
            tui_app_toast(app, "Maximum voice note length reached; sent", 0);
        }
        if (app->toast[0] && now > app->toast_until_ms) { app->toast[0] = '\0'; app->dirty = 1; }
        if (idle_tracker_is_idle(&app->idle, 2)) set_active(app, 0);
        update_tab(app, now);
        maybe_screensaver(app);
        follow_camera(app, now);
        if (app->splash.active) {
            if (splash_view_active(&app->splash, now)) { run_splash_frame(app, now); continue; }
            clearok(curscr, TRUE);                          /* the first real frame repaints everything */
            app->dirty = 1;
        }
        int64_t minute = (int64_t)time(NULL) / 60;
        if (changes_any(&ch) || minute != app->last_minute) app->dirty = 1;
        if (animating(app, now) && now - app->last_frame_ms >= 250) app->dirty = 1;
        if (app->dirty) {
            tui_render_frame(app, now);
            app->dirty = 0;
            keep_message_window(app);
            app->last_frame_ms = now;
            app->last_minute = minute;
        }
        read_input(app);
    }

    save_draft(app);
    messaging_manager_set_active(app->deps.messaging, 0);
    tui_app_accounts_set_active(app, 0);
    terminal_modes(0, 0);
    endwin();
    return 0;
}
