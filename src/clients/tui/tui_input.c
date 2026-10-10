#include "tui_app_state.h"
#include "clients/tui/escape_sequence.h"
#include "engines/emoji_shortcode.h"
#include "engines/emoticon_converter.h"

#include "clients/tui/header_bar.h"
#include "clients/tui/mac_option_keys.h"
#include "utilities/platform.h"
#include "clients/tui/tui_key_newline.h"
#include "utilities/clock_util.h"
#include "utilities/dropped_path.h"
#include "utilities/log.h"
#include "utilities/str_util.h"
#include "utilities/utf8_text.h"

#include <ncurses.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CTRL(c) ((c) & 0x1f)
#define PASTE_MAX 8192

static const Settings *settings(TuiApp *app) { return settings_manager_current(app->deps.settings); }

static void pick_emoji_suggestion(TuiApp *app, int index);

/* Ctrl+End: curses reports it as an extended key named "kEND5". */
static int is_ctrl_end(int is_key, int ch) {
    if (!is_key || ch <= KEY_MAX) return 0;
    const char *name = keyname(ch);
    return name && strcmp(name, "kEND5") == 0;
}

static int is_enter(int is_key, int ch) {
    return (!is_key && (ch == '\n' || ch == '\r')) || (is_key && ch == KEY_ENTER);
}

static int is_esc(int is_key, int ch) { return !is_key && ch == 27; }

static int shift_key(int mods) { return (mods & ESCAPE_MOD_SHIFT) != 0; }

/* Somewhere that takes text, where å or ø may be meant as letters. */
static int typing_text(TuiApp *app) {
    return app->focus == TUI_FOCUS_COMPOSER || app->chat_list.filtering || app->search.open || app->settings_panel.open ||
           app->file_picker.open || app->emoji_picker.open || profile_dialogs_is_open(&app->profile) ||
           app->status_composer.open || tui_app_show_login(app);
}

static const Chat *chat_list(TuiApp *app, int *count) {
    return tui_app_chat_rows(app, count);
}

/* ---- bracketed paste (file drops) ---------------------------------------- */

/* After ESC, checks for "[200~" (start of a bracketed paste). Only the
 * characters actually read are pushed back, so a bare Esc stays an Esc. */
/* Collects pasted text up to the closing ESC [ 2 0 1 ~. Returns UTF-8. */
static char *read_paste_body(void) {
    static wchar_t buf[PASTE_MAX];
    int n = 0;
    wtimeout(stdscr, 50);
    for (;;) {
        wint_t wc;
        if (wget_wch(stdscr, &wc) == ERR) break;
        if (n < PASTE_MAX) buf[n++] = (wchar_t)wc;
        if (n >= 6 && buf[n - 6] == 27 && buf[n - 5] == '[' && buf[n - 4] == '2' && buf[n - 3] == '0' &&
            buf[n - 2] == '1' && buf[n - 1] == '~') {
            n -= 6;
            break;
        }
    }
    wtimeout(stdscr, 100);
    return utf8_from_wide(buf, (size_t)n);
}

static void handle_paste(TuiApp *app, const char *text) {
    if (app->agents.open) { agents_panel_paste(&app->agents, text); app->dirty = 1; return; }
    if (profile_dialogs_is_open(&app->profile)) { profile_dialogs_paste(&app->profile, text); return; }
    if (status_feed_dialogs_replying(&app->feed)) { status_feed_dialogs_paste(&app->feed, text); return; }
    if (app->accounts_dialog.open) { accounts_dialog_paste(&app->accounts_dialog, text); app->dirty = 1; return; }
    if (app->voice_languages.open) { chat_toggle_dialog_paste(&app->voice_languages, text); return; }
    if (app->self_chats.open) { chat_toggle_dialog_paste(&app->self_chats, text); return; }
    if (app->forward_picker.open) { chat_picker_paste(&app->forward_picker, text); return; }
    if (app->scheduled_list.open) {
        if (app->scheduled_list.editing) text_field_paste(&app->scheduled_list.when, text);
        return;
    }
    if (app->status_composer.open) {
        char dropped[1024];                                     /* a file dropped or pasted on the terminal */
        if (!text[0]) {
            tui_app_status_paste_picture(app);                  /* an empty paste: the clipboard holds a picture */
        } else if (dropped_path_resolve(text, dropped, sizeof(dropped)) == 0 &&
                   status_kind_has_media(status_manager_kind_for_file(app->deps.statuses, dropped))) {
            tui_app_account_file(app, FILE_PICKER_FOR_STATUS, dropped);
        } else {
            status_composer_dialog_paste(&app->status_composer, text);
        }
        return;
    }
    if (app->settings_panel.open) { settings_panel_paste(&app->settings_panel, text); return; }
    if (app->search.open) {
        for (const char *c = text; *c; c++) if ((unsigned char)*c >= 32) search_overlay_key(&app->search, 0, (unsigned char)*c);
        tui_app_run_search(app);
        return;
    }
    if (tui_app_show_login(app)) {
        for (const char *c = text; *c; c++) if (*c >= '0' && *c <= '9') login_view_key(&app->login, 0, *c);
        return;
    }
    if (!text[0]) { tui_app_paste_image(app); return; }      /* an empty paste: the clipboard holds a picture */
    char path[1024];
    if (messaging_manager_open_jid(app->deps.messaging)[0] && dropped_path_resolve(text, path, sizeof(path)) == 0) {
        tui_app_attach(app, path);
        return;
    }
    wchar_t wide[PASTE_MAX];
    size_t n = mbstowcs(wide, text, PASTE_MAX - 1);
    if (n == (size_t)-1) return;
    for (size_t i = 0; i < n; i++) {
        wchar_t c = wide[i] == L'\r' ? L'\n' : wide[i];
        if (c == L'\n' || c == L'\t' || c >= 32) composer_view_insert(&app->composer, c == L'\t' ? L' ' : c);
    }
    app->focus = TUI_FOCUS_COMPOSER;
}

/* ---- helpers ------------------------------------------------------------ */

static void toggle_sidebar(TuiApp *app) {
    Settings s = *settings(app);
    s.sidebar_collapsed = !s.sidebar_collapsed;
    tui_app_apply_settings(app, &s);
    if (s.sidebar_collapsed && app->focus == TUI_FOCUS_CHATS) app->focus = TUI_FOCUS_COMPOSER;
}

static void save_sidebar_width(TuiApp *app, int width) {
    Settings s = *settings(app);
    s.sidebar_width = width < 20 ? 20 : width > 80 ? 80 : width;
    s.sidebar_collapsed = 0;
    tui_app_apply_settings(app, &s);
}

static void cycle_focus(TuiApp *app, int dir) {
    int has_sidebar = app->layout.sidebar.w > 0;
    int f = (int)app->focus;
    do {
        f = (f + dir + 3) % 3;
    } while (f == TUI_FOCUS_CHATS && !has_sidebar);
    app->focus = (TuiFocus)f;
}

/* Scrolls the conversation up; past the oldest loaded message it loads more. */
static void scroll_older(TuiApp *app, int rows) {
    MessageView *v = &app->message_view;
    int body = app->layout.chat.h - 1;
    int max_scroll = v->row_count - body;
    message_view_scroll(v, rows);
    if (v->scroll > max_scroll) tui_app_load_older(app);
}

static void select_chat_entry(TuiApp *app) {
    int count = 0;
    const Chat *chats = chat_list(app, &count);
    const char *jid = chat_list_view_activate(&app->chat_list, chats, count);
    tui_app_save_folding(app);
    if (jid) tui_app_open_row(app, chat_list_view_selected_chat(&app->chat_list, chats));
}

/* ---- overlays ----------------------------------------------------------- */

/* Popups get keys first, topmost first. Returns 1 when one handled it. */
static int handle_overlays_key(TuiApp *app, int is_key, int ch) {
    if (call_manager_ringing(app->deps.calls)) {                   /* a ringing call comes first */
        IncomingCallChoice choice = incoming_call_key(is_key, ch);
        if (choice != INCOMING_CALL_NONE) { tui_app_call_choice(app, choice); return 1; }
        return 1;
    }
    if (app->confirm.open) {
        if (confirm_dialog_key(&app->confirm, is_key, ch) == POPUP_CHOSEN) tui_app_confirmed(app);
        app->dirty = 1;
        return 1;
    }
    if (app->agents.open) { tui_app_agents_key(app, is_key, ch); return 1; }
    if (app->viewer.open) {
        int count = 0;
        const Message *msgs = tui_app_messages(app, &count);
        tui_app_viewer_action(app, image_viewer_key(&app->viewer, msgs, count, is_key, ch));
        return 1;
    }
    if (app->contact.open) {
        if (contact_panel_key(&app->contact, is_key, ch) == POPUP_CHOSEN) tui_app_contact_action(app);
        app->dirty = 1;
        return 1;
    }
    if (app->reader.open) { text_reader_key(&app->reader, is_key, ch); return 1; }
    if (app->theme_picker.open) {
        PopupResult r = theme_picker_overlay_key(&app->theme_picker, settings_manager_themes(app->deps.settings), is_key, ch);
        if (r == POPUP_CHANGED) tui_app_preview_chat_theme(app);
        else if (r == POPUP_CHOSEN) tui_app_finish_theme_picker(app, 1);
        else if (r == POPUP_CLOSED) tui_app_finish_theme_picker(app, 0);
        return 1;
    }
    if (app->search.open) {
        PopupResult r = search_overlay_key(&app->search, is_key, ch);
        if (r == POPUP_CHANGED) tui_app_run_search(app);
        else if (r == POPUP_CHOSEN) tui_app_choose_search_result(app);
        return 1;
    }
    if (app->message_menu.open) {
        if (message_menu_key(&app->message_menu, is_key, ch) == POPUP_CHOSEN) tui_app_apply_message_action(app);
        return 1;
    }
    if (app->send_accounts.open) {
        tui_app_send_accounts_request(app, send_account_dialog_key(&app->send_accounts, is_key, ch));
        return 1;
    }
    if (app->accounts_dialog.open) {
        tui_app_accounts_request(app, accounts_dialog_key(&app->accounts_dialog, is_key, ch));
        return 1;
    }
    if (app->voice_languages.open) {
        tui_app_voice_languages_request(app, chat_toggle_dialog_key(&app->voice_languages, is_key, ch));
        return 1;
    }
    if (app->self_chats.open) {
        tui_app_self_chats_request(app, chat_toggle_dialog_key(&app->self_chats, is_key, ch));
        return 1;
    }
    if (app->forward_picker.open) {
        tui_app_forward_request(app, chat_picker_key(&app->forward_picker, is_key, ch));
        return 1;
    }
    if (app->scheduled_list.open) {
        tui_app_scheduled_request(app, scheduled_list_dialog_key(&app->scheduled_list, is_key, ch));
        app->dirty = 1;
        return 1;
    }
    if (app->emoji_picker.open) {
        if (emoji_picker_key(&app->emoji_picker, app->deps.emoji, is_key, ch) == POPUP_CHOSEN) tui_app_emoji_chosen(app);
        return 1;
    }
    if (app->options.open) {
        if (chat_options_menu_key(&app->options, is_key, ch) == POPUP_CHOSEN) tui_app_apply_chat_option(app);
        return 1;
    }
    if (app->attach_menu.open) {
        if (attach_menu_key(&app->attach_menu, is_key, ch) == POPUP_CHOSEN) tui_app_apply_attach_choice(app);
        app->dirty = 1;
        return 1;
    }
    if (app->camera_view.open) {
        tui_app_camera_action(app, camera_view_key(&app->camera_view, is_key, ch));
        return 1;
    }
    if (app->message_info.open) {
        message_info_panel_key(&app->message_info, is_key, ch);
        return 1;
    }
    if (app->palette.open) {
        if (reaction_palette_key(&app->palette, is_key, ch) == POPUP_CHOSEN) tui_app_reaction_chosen(app);
        return 1;
    }
    if (app->file_picker.open) {
        if (file_picker_key(&app->file_picker, is_key, ch) == FILE_PICKER_PICKED) tui_app_file_picked(app, app->file_picker.picked);
        return 1;
    }
    if (profile_dialogs_is_open(&app->profile)) {
        int camera = media_manager_camera_available(app->deps.media);
        int photo = profile_manager_picture(app->deps.profiles, messaging_manager_user_jid(app->deps.messaging)) != NULL;
        tui_app_profile_request(app, profile_dialogs_key(&app->profile, is_key, ch, camera, photo));
        app->dirty = 1;
        return 1;
    }
    if (status_feed_dialogs_is_open(&app->feed)) {
        tui_app_statuses_request(app, status_feed_dialogs_key(&app->feed, is_key, ch));
        app->dirty = 1;
        return 1;
    }
    if (app->status_composer.open) {
        tui_app_status_request(app, status_composer_dialog_key(&app->status_composer, is_key, ch));
        app->dirty = 1;
        return 1;
    }
    if (app->settings_panel.open) {
        settings_panel_key(&app->settings_panel, is_key, ch, clock_now_ms());
        return 1;
    }
    return 0;
}

/* ---- mouse -------------------------------------------------------------- */

static int handle_overlays_mouse(TuiApp *app, const MEVENT *ev, int wheel, int press) {
    int y = ev->y, x = ev->x;
    if (call_manager_ringing(app->deps.calls)) {
        if (press) tui_app_call_choice(app, incoming_call_click(&app->call_view, y, x));
        return 1;
    }
    if (app->confirm.open) {
        if (press && confirm_dialog_click(&app->confirm, y, x) == POPUP_CHOSEN) tui_app_confirmed(app);
        app->dirty = 1;
        return 1;
    }
    if (app->agents.open) {
        if (press && header_bar_hit_chats_tab(&app->header_hits, y, x)) { app->agents.open = 0; app->dirty = 1; return 1; }
        if (press && y == app->layout.header.y) return 0;            /* the rest of the header still works */
        if (press) tui_app_agents_click(app, y, x);
        return 1;
    }
    if (app->contact.open && !app->viewer.open) {
        if (wheel) contact_panel_wheel(&app->contact, wheel * 3);
        else if (press && contact_panel_hit_portrait(&app->contact, y, x)) tui_app_show_portrait(app, app->contact.jid);
        else if (press && contact_panel_click(&app->contact, y, x) == POPUP_CHOSEN) tui_app_contact_action(app);
        app->dirty = 1;
        return 1;
    }
    if (app->viewer.open) {
        int count = 0;
        const Message *msgs = tui_app_messages(app, &count);
        if (wheel) tui_app_viewer_action(app, image_viewer_wheel(&app->viewer, msgs, count, wheel));
        else if (press) tui_app_viewer_action(app, image_viewer_click(&app->viewer, msgs, count, y, x));
        return 1;
    }
    if (app->reader.open) { if (wheel) text_reader_wheel(&app->reader, wheel); return 1; }
    if (app->theme_picker.open) {
        IThemeRepository *themes = settings_manager_themes(app->deps.settings);
        if (wheel) { theme_picker_overlay_wheel(&app->theme_picker, themes, wheel); tui_app_preview_chat_theme(app); }
        else if (press) {
            PopupResult r = theme_picker_overlay_click(&app->theme_picker, y, x);
            if (r == POPUP_CHANGED) tui_app_preview_chat_theme(app);
            else if (r == POPUP_CHOSEN) tui_app_finish_theme_picker(app, 1);
        }
        return 1;
    }
    if (app->search.open) {
        if (wheel) search_overlay_wheel(&app->search, wheel);
        else if (press && search_overlay_click(&app->search, y, x) == POPUP_CHOSEN) tui_app_choose_search_result(app);
        return 1;
    }
    if (app->message_menu.open) {
        if (press && message_menu_click(&app->message_menu, y, x) == POPUP_CHOSEN) tui_app_apply_message_action(app);
        return 1;
    }
    if (app->scheduled_list.open) {
        if (press) tui_app_scheduled_request(app, scheduled_list_dialog_click(&app->scheduled_list, y, x));
        app->dirty = 1;
        return 1;
    }
    if (app->send_accounts.open) {
        if (press) tui_app_send_accounts_request(app, send_account_dialog_click(&app->send_accounts, y, x));
        app->dirty = 1;
        return 1;
    }
    if (app->accounts_dialog.open) {
        if (press) tui_app_accounts_request(app, accounts_dialog_click(&app->accounts_dialog, y, x));
        app->dirty = 1;
        return 1;
    }
    if (app->voice_languages.open) {
        if (wheel) chat_toggle_dialog_wheel(&app->voice_languages, wheel);
        else if (press) tui_app_voice_languages_request(app, chat_toggle_dialog_click(&app->voice_languages, y, x));
        app->dirty = 1;
        return 1;
    }
    if (app->self_chats.open) {
        if (wheel) chat_toggle_dialog_wheel(&app->self_chats, wheel);
        else if (press) tui_app_self_chats_request(app, chat_toggle_dialog_click(&app->self_chats, y, x));
        app->dirty = 1;
        return 1;
    }
    if (app->forward_picker.open) {
        if (wheel) chat_picker_wheel(&app->forward_picker, wheel);
        else if (press) tui_app_forward_request(app, chat_picker_click(&app->forward_picker, y, x));
        app->dirty = 1;
        return 1;
    }
    if (app->emoji_picker.open) {
        if (wheel) emoji_picker_wheel(&app->emoji_picker, wheel);
        else if (press && emoji_picker_click(&app->emoji_picker, app->deps.emoji, y, x) == POPUP_CHOSEN) tui_app_emoji_chosen(app);
        return 1;
    }
    if (app->options.open) {
        if (press && chat_options_menu_click(&app->options, y, x) == POPUP_CHOSEN) tui_app_apply_chat_option(app);
        return 1;
    }
    if (app->attach_menu.open) {
        if (press && attach_menu_click(&app->attach_menu, y, x) == POPUP_CHOSEN) tui_app_apply_attach_choice(app);
        app->dirty = 1;
        return 1;
    }
    if (app->camera_view.open) return 1;                      /* keys only: Space, Enter, R, Esc */
    if (app->message_info.open) {
        if (press) message_info_panel_click(&app->message_info, y, x);
        else if (wheel) message_info_panel_key(&app->message_info, 1, wheel < 0 ? KEY_UP : KEY_DOWN);
        return 1;
    }
    if (app->palette.open) {
        if (press && reaction_palette_click(&app->palette, y, x) == POPUP_CHOSEN) tui_app_reaction_chosen(app);
        return 1;
    }
    if (app->file_picker.open) {
        if (wheel) file_picker_wheel(&app->file_picker, wheel);
        else if (press && file_picker_click(&app->file_picker, y, x) == FILE_PICKER_PICKED) tui_app_file_picked(app, app->file_picker.picked);
        return 1;
    }
    if (profile_dialogs_is_open(&app->profile)) {
        if (press) {
            int camera = media_manager_camera_available(app->deps.media);
            int photo = profile_manager_picture(app->deps.profiles, messaging_manager_user_jid(app->deps.messaging)) != NULL;
            tui_app_profile_request(app, profile_dialogs_click(&app->profile, y, x, camera, photo));
        }
        app->dirty = 1;
        return 1;
    }
    if (status_feed_dialogs_is_open(&app->feed)) {
        if (wheel) status_feed_dialogs_wheel(&app->feed, wheel);
        else if (press) tui_app_statuses_request(app, status_feed_dialogs_click(&app->feed, y, x));
        app->dirty = 1;
        return 1;
    }
    if (app->status_composer.open) {
        if (press) tui_app_status_request(app, status_composer_dialog_click(&app->status_composer, y, x));
        app->dirty = 1;
        return 1;
    }
    if (app->settings_panel.open) {
        if (wheel) settings_panel_wheel(&app->settings_panel, wheel);
        else if (press) settings_panel_click(&app->settings_panel, y, x, clock_now_ms());
        return 1;
    }
    return 0;
}

static void handle_mouse_event(TuiApp *app, MEVENT ev) {
    LOG_DEBUG("mouse y=%d x=%d bstate=%#lx", ev.y, ev.x, (unsigned long)ev.bstate);
    TuiLayout *l = &app->layout;
    int wheel = (ev.bstate & BUTTON4_PRESSED) ? -1 : (ev.bstate & BUTTON5_PRESSED) ? 1 : 0;
    int press = (ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) != 0;
    /* A fast click can arrive as a press and then a "clicked" at the same
     * spot; acting on both would open something and close it again. */
    static int last_y = -1, last_x = -1;
    static int64_t last_press_ms = 0;
    int64_t now_ms = clock_now_ms();
    if ((ev.bstate & BUTTON1_CLICKED) && !(ev.bstate & BUTTON1_PRESSED) &&
        ev.y == last_y && ev.x == last_x && now_ms - last_press_ms < 400) {
        press = 0;
    }
    if (ev.bstate & BUTTON1_PRESSED) { last_y = ev.y; last_x = ev.x; last_press_ms = now_ms; }
    int right = (ev.bstate & (BUTTON3_PRESSED | BUTTON3_CLICKED)) != 0;
    /* Ctrl+click is the Mac right-click; terminals report it as a left click with Ctrl held. */
    if (press && (ev.bstate & BUTTON_CTRL)) { right = 1; press = 0; }

    /* Dragging the divider resizes the chat list. */
    if (app->dragging_divider) {
        if (ev.bstate & BUTTON1_RELEASED) { app->dragging_divider = 0; save_sidebar_width(app, app->drag_width); }
        else app->drag_width = ev.x < 20 ? 20 : ev.x;
        return;
    }
    /* Dragging a chat into or out of the Pinned group. */
    if (app->chat_list.drag_jid[0]) {
        int count = 0;
        const Chat *chats = chat_list(app, &count);
        if (ev.bstate & BUTTON1_RELEASED) {
            char jid[128];
            AccountId dragged = app->chat_list.drag_account;
            int pin = chat_list_view_drag_end(&app->chat_list, chats, count, l->sidebar, ev.y, jid, sizeof(jid));
            if (pin >= 0 && tui_app_use_account(app, dragged) == 0) {
                messaging_manager_toggle_pin(app->deps.messaging, jid);
                tui_app_toast(app, pin ? "\xF0\x9F\x93\x8C Pinned" : "Unpinned", 0);
            }
            app->dirty = 1;
            return;
        }
        if (ev.bstate & REPORT_MOUSE_POSITION) {
            chat_list_view_drag_move(&app->chat_list, chats, count, l->sidebar, ev.y);
            app->dirty = 1;
            return;
        }
    }
    if ((ev.bstate & BUTTON1_PRESSED) && l->divider.w && ui_rect_contains(l->divider, ev.y, ev.x)) {
        app->dragging_divider = 1;
        app->drag_width = l->sidebar.w;
        return;
    }
    if (handle_overlays_mouse(app, &ev, wheel, press || right)) return;
    if (press && header_bar_hit_gear(l->header, ev.y, ev.x)) { settings_panel_open(&app->settings_panel); return; }
    if (press && !tui_app_show_login(app) && header_bar_hit_post(&app->header_hits, ev.y, ev.x)) { tui_app_open_status(app); return; }
    if (press && !tui_app_show_login(app) && header_bar_hit_statuses(&app->header_hits, ev.y, ev.x)) { tui_app_open_statuses(app); return; }
    if (press && header_bar_hit_agents(&app->header_hits, ev.y, ev.x)) { if (!app->agents.open) tui_app_open_agents(app); return; }
    if (press && header_bar_hit_chats_tab(&app->header_hits, ev.y, ev.x)) { app->agents.open = 0; app->dirty = 1; return; }
    if (press && !tui_app_show_login(app) && header_bar_hit_account(&app->header_hits, ev.y, ev.x)) { tui_app_cycle_account_filter(app); return; }
    if (press && !tui_app_show_login(app) && header_bar_hit_profile(&app->header_hits, ev.y, ev.x)) { tui_app_open_profile(app); return; }
    if (press && header_bar_hit_menu(l->header, ev.y, ev.x)) { toggle_sidebar(app); return; }
    if (tui_app_show_login(app)) {
        if (press && login_view_click(&app->login, ev.y, ev.x) == LOGIN_ACTION_NEW_QR) messaging_manager_request_qr(app->deps.messaging);
        return;
    }
    if (press && app->mention_suggestions.count) {
        int hit = mention_suggestions_hit(&app->mention_suggestions, ev.y, ev.x);
        if (hit >= 0) { tui_app_pick_mention(app, hit); return; }
        mention_suggestions_close(&app->mention_suggestions);
    }
    if (press && app->emoji_suggestions.count) {
        int hit = emoji_suggestions_hit(&app->emoji_suggestions, ev.y, ev.x);
        if (hit >= 0) { pick_emoji_suggestion(app, hit); return; }
        emoji_suggestions_close(&app->emoji_suggestions);
    }
    if (press && composer_view_hit_clear(&app->composer, ev.y, ev.x)) { tui_app_ask_clear_input(app); return; }
    if (press && composer_view_hit_emoji(&app->composer, ev.y, ev.x)) { tui_app_open_emoji(app, EMOJI_PICKER_FOR_INPUT, NULL, ""); return; }
    if (press && composer_view_hit_attach(&app->composer, ev.y, ev.x)) { tui_app_open_attach_menu(app); return; }
    if (press && composer_view_hit_send(&app->composer, ev.y, ev.x)) { tui_app_send_composer(app); return; }

    if (right && !tui_app_show_login(app)) {                /* right-click: context menus */
        if (l->sidebar.w && ui_rect_contains(l->sidebar, ev.y, ev.x) && chat_list_view_hit(&app->chat_list, l->sidebar, ev.y)) {
            const char *jid = tui_app_take_selected(app);
            if (jid) { char copy[128]; str_copy(copy, sizeof(copy), jid); tui_app_open_chat_options(app, copy); }
        } else if (ui_rect_contains(l->chat, ev.y, ev.x)) {
            int idx = message_view_hit(&app->message_view, ev.y, ev.x);
            if (idx >= 0) { app->focus = TUI_FOCUS_MESSAGES; tui_app_open_message_menu(app, idx, ev.y, ev.x); }
        }
        return;
    }
    if (press && message_view_hit_newer(&app->message_view, ev.y, ev.x)) { tui_app_show_latest(app); return; }
    if (l->sidebar.w && ui_rect_contains(l->sidebar, ev.y, ev.x)) {
        if (wheel) chat_list_view_move(&app->chat_list, wheel);
        else if (press && chat_list_view_hit(&app->chat_list, l->sidebar, ev.y)) {
            app->focus = TUI_FOCUS_CHATS;
            if (ev.bstate & BUTTON1_PRESSED) {                 /* held down: may become a drag */
                int count = 0;
                chat_list_view_drag_begin(&app->chat_list, chat_list(app, &count));
            }
            select_chat_entry(app);
        }
    } else if (ui_rect_contains(l->chat, ev.y, ev.x)) {
        if (wheel < 0) scroll_older(app, 3);
        else if (wheel > 0) message_view_scroll(&app->message_view, -3);
        else if (press && message_view_hit_portrait(&app->message_view, ev.y, ev.x)) {
            tui_app_show_portrait(app, messaging_manager_open_jid(app->deps.messaging));
        } else if (press && message_view_hit_title(&app->message_view, ev.y, ev.x)) {
            tui_app_open_contact(app, messaging_manager_open_jid(app->deps.messaging));
        } else if (press) {
            int quoted = message_view_hit_quote(&app->message_view, ev.y, ev.x);
            if (quoted >= 0) { tui_app_go_to_quote(app, quoted); return; }     /* a click on a quote jumps to it */
            int idx = message_view_hit(&app->message_view, ev.y, ev.x);
            if (idx >= 0) {
                app->message_view.selected = idx;
                tui_app_activate_message(app, idx);
            }
            /* A click anywhere in the conversation leaves the cursor in the input, ready to type. */
            if (messaging_manager_open_jid(app->deps.messaging)[0]) app->focus = TUI_FOCUS_COMPOSER;
        }
    } else if (ui_rect_contains(l->composer, ev.y, ev.x)) {
        if (wheel) composer_view_scroll(&app->composer, wheel);
        else if (press) app->focus = TUI_FOCUS_COMPOSER;
    }
}

/* One KEY_MOUSE can stand for several queued events: a press and its
 * release, or clicks that came in together. Reading only one left the rest
 * queued and later clicks, right-clicks above all, went missing. */
static void handle_mouse(TuiApp *app) {
    MEVENT ev;
    for (int i = 0; i < 64 && getmouse(&ev) == OK; i++) handle_mouse_event(app, ev);
}

/* ---- keys --------------------------------------------------------------- */

static int handle_global(TuiApp *app, int is_key, int ch) {
    if (is_key && ch == KEY_F(2)) { settings_panel_open(&app->settings_panel); return 1; }
    if (is_key && ch == KEY_F(3)) { tui_app_open_agents(app); return 1; }
    if (tui_app_show_login(app)) return 0;
    if (!is_key && ch == CTRL('b')) { toggle_sidebar(app); return 1; }
    if (!is_key && ch == CTRL('v')) { tui_app_paste_image(app); return 1; }   /* reaches tawk when the terminal passes it on */
    if (!is_key && ch == CTRL('d')) { tui_app_toggle_dnd(app); return 1; }
    if (!is_key && ch == CTRL('r')) { tui_app_toggle_recording(app); return 1; }
    if (!is_key && ch == CTRL('o')) { tui_app_open_file_picker(app, FILE_PICKER_FOR_ATTACHMENT); return 1; }
    if (!is_key && ch == CTRL('k')) { tui_app_open_search(app, ""); return 1; }
    if (!is_key && ch == CTRL('l')) { if (!tui_app_lock(app)) tui_app_start_screensaver(app); return 1; }
    if (!is_key && ch == CTRL('e')) { tui_app_open_emoji(app, EMOJI_PICKER_FOR_INPUT, NULL, ""); return 1; }
    if (!is_key && ch == CTRL('f')) { app->focus = TUI_FOCUS_CHATS; app->chat_list.filtering = 1; return 1; }
    if (!is_key && ch == CTRL('n')) {
        int count = 0;
        const Chat *chats = chat_list(app, &count);
        chat_list_view_sync(&app->chat_list, chats, count);
        const char *jid = chat_list_view_next_unread(&app->chat_list, chats);
        if (jid) { char copy[128]; str_copy(copy, sizeof(copy), jid); tui_app_open_chat(app, copy); }
        else tui_app_toast(app, "No unread chats", 0);
        return 1;
    }
    if (is_key && ch == KEY_PPAGE) { scroll_older(app, app->layout.chat.h - 2); return 1; }
    if (is_key && ch == KEY_NPAGE) { message_view_scroll(&app->message_view, -(app->layout.chat.h - 2)); return 1; }
    if (!is_key && ch == '\t' && !(app->focus == TUI_FOCUS_COMPOSER && (app->suggestions.count || app->emoji_suggestions.count || app->mention_suggestions.count))) { cycle_focus(app, 1); return 1; }
    if (is_key && ch == KEY_BTAB) { cycle_focus(app, -1); return 1; }
    return 0;
}

static void handle_chats(TuiApp *app, int is_key, int ch, int alt) {
    int count = 0;
    const Chat *chats = chat_list(app, &count);
    ChatListView *v = &app->chat_list;
    chat_list_view_sync(v, chats, count);
    if (v->filtering) {
        if (is_esc(is_key, ch)) { v->filtering = 0; v->filter[0] = '\0'; }
        else if (is_enter(is_key, ch)) { v->filtering = 0; select_chat_entry(app); }
        else if (is_key && (ch == KEY_UP || ch == KEY_DOWN)) chat_list_view_move(v, ch == KEY_UP ? -1 : 1);
        else {
            chat_list_view_filter_key(v, is_key && ch == KEY_BACKSPACE ? KEY_BACKSPACE : ch);
            if (!v->filter[0]) v->filtering = 0;              /* cleared: the search bar goes away */
        }
        return;
    }
    const char *jid = chat_list_view_selected_jid(v, chats);
    char copy[128] = "";
    if (jid) str_copy(copy, sizeof(copy), jid);
    /* What these keys do goes through the selected chat's own account. */
    if (alt && !is_key && copy[0] && (ch == 'o' || ch == 'm' || ch == 'p' || ch == 'a')) tui_app_take_selected(app);
    if (is_key && ch == KEY_UP) chat_list_view_move(v, -1);
    else if (is_key && ch == KEY_DOWN) chat_list_view_move(v, 1);
    else if (is_key && ch == KEY_HOME) chat_list_view_move(v, -v->entry_count);
    else if (is_key && ch == KEY_END) chat_list_view_move(v, v->entry_count);
    else if (is_key && (ch == KEY_LEFT || ch == KEY_RIGHT) && chat_list_view_fold(v, chats, count, ch == KEY_RIGHT)) tui_app_save_folding(app);
    else if (is_enter(is_key, ch) || (is_key && ch == KEY_RIGHT)) select_chat_entry(app);
    else if (is_esc(is_key, ch) || (is_key && ch == KEY_LEFT)) chat_list_view_back(v, chats, count);
    else if (alt && !is_key && ch == 'o' && copy[0]) tui_app_open_chat_options(app, copy);
    else if (alt && !is_key && ch == 'm' && copy[0]) messaging_manager_toggle_mute(app->deps.messaging, copy);
    else if (alt && !is_key && ch == 'p' && copy[0]) messaging_manager_toggle_pin(app->deps.messaging, copy);
    else if (alt && !is_key && ch == 'a' && copy[0]) {
        const ChatListEntry *e = &v->entries[v->selected];
        int archived = e->kind == CHAT_LIST_ENTRY_CHAT && chats[e->chat].is_archived > 0;
        messaging_manager_set_archived(app->deps.messaging, copy, !archived);
        tui_app_toast(app, archived ? "Moved back to chats" : "\xF0\x9F\x97\x84  Archived", 0);
    } else if (!alt && !is_key && ch == '/') {
        v->filter[0] = '\0';                                     /* an empty search bar, waiting */
        v->filtering = 1;
    } else if (!alt && !is_key && ch > 32 && ch != 127) {
        /* Typing in the chat list searches it by name. */
        v->filter[0] = '\0';
        v->filtering = 1;
        chat_list_view_filter_key(v, ch);
    }
}

static void handle_messages(TuiApp *app, int is_key, int ch, int alt) {
    int count = 0;
    const Message *msgs = tui_app_messages(app, &count);
    MessageView *v = &app->message_view;
    /* Focus can arrive without a selection (Tab): act on the newest message. */
    if (v->selected < 0 && count > 0 && (alt || is_key || is_enter(is_key, ch))) message_view_select(v, count, 0);
    /* What is done to a message goes through the account it belongs to. */
    if (v->selected >= 0 && (alt || is_enter(is_key, ch) || (is_key && ch == KEY_DC))) {
        tui_app_follow_message(app, v->selected);
        msgs = tui_app_messages(app, &count);
    }
    if (is_key && ch == KEY_UP) {
        if (v->selected == 0) tui_app_load_older(app);
        else message_view_select(v, count, -1);
    } else if (is_key && ch == KEY_DOWN) message_view_select(v, count, 1);
    else if ((is_key && ch == KEY_END) || is_ctrl_end(is_key, ch)) tui_app_show_latest(app);
    else if (is_enter(is_key, ch) && v->selected >= 0) tui_app_activate_message(app, v->selected);
    else if (alt && !is_key && ch == 'q' && v->selected >= 0) tui_app_start_reply(app, v->selected);
    else if (alt && !is_key && ch == 'e' && v->selected >= 0) tui_app_open_reactions(app, v->selected);
    else if (alt && !is_key && ch == 'E' && v->selected >= 0) tui_app_start_edit(app, v->selected);
    else if (alt && !is_key && ch == 'm' && v->selected >= 0) {
        tui_app_open_message_menu(app, v->selected, app->layout.chat.y + app->layout.chat.h / 2, app->layout.chat.x + app->layout.chat.w / 3);
    }
    else if (is_key && ch == KEY_DC && v->selected >= 0) {
        tui_app_open_delete_menu(app, v->selected, app->layout.chat.y + app->layout.chat.h / 2, app->layout.chat.x + app->layout.chat.w / 3);
    }
    else if (alt && !is_key && ch == 'o') tui_app_open_chat_options(app, messaging_manager_open_jid(app->deps.messaging));
    else if (alt && !is_key && ch == 'r' && v->selected >= 0 && v->selected < count) {
        if (messaging_manager_retry_message(app->deps.messaging, msgs[v->selected].id) == 0) tui_app_toast(app, "Retrying\xE2\x80\xA6", 0);
    } else if (is_esc(is_key, ch)) {
        v->selected = -1;
        app->focus = TUI_FOCUS_COMPOSER;
    } else if (!alt && !is_key && ch >= 32 && ch != 127) {
        /* Typing goes to the input, wherever the selection is. */
        app->focus = TUI_FOCUS_COMPOSER;
        composer_view_key(&app->composer, 0, ch);
    }
}

/* The typed text when it looks like the start of a /command, else NULL. */
static char *typed_command(TuiApp *app) {
    if (app->composer.length == 0 || app->composer.text[0] != L'/') return NULL;
    char *text = composer_view_text(&app->composer);
    if (text && (text[1] == '/' || strchr(text, ' ') || strchr(text, '\n'))) { free(text); return NULL; }
    return text;
}

/* Replaces the "(word)" with suggestion `index` and closes the strip. */
static void pick_emoji_suggestion(TuiApp *app, int index) {
    EmojiSuggestions *es = &app->emoji_suggestions;
    if (index >= 0 && index < es->count) es->selected = index;
    const char *glyph = emoji_suggestions_glyph(es, app->deps.emoji);
    if (glyph) composer_view_replace(&app->composer, es->start, es->end, glyph);
    emoji_suggestions_close(es);
}

/* Keeps the strip in step with the shortcode at the caret: "(hu" lists what
 * fits, and a closing ")" with only one fit replaces it at once. */
static void refresh_shortcode(TuiApp *app) {
    EmojiSuggestions *es = &app->emoji_suggestions;
    char code[40];
    int start = 0, closed = 0;
    if (!settings(app)->convert_emoticons ||
        !composer_view_shortcode(&app->composer, code, sizeof(code), &start, &closed)) {
        emoji_suggestions_close(es);
        es->dismissed = 0;
        return;
    }
    if (start + 1 == es->dismissed) return;
    int matches[EMOJI_SHORTCODE_MAX];
    int n = emoji_shortcode_match(app->deps.emoji, code, matches, EMOJI_SHORTCODE_MAX);
    if (closed && n == 1) {
        const Emoji *e = app->deps.emoji->at(app->deps.emoji, matches[0]);
        emoji_suggestions_close(es);
        if (e) composer_view_replace(&app->composer, start, app->composer.cursor, e->glyph);
    } else if (n > 0) {
        emoji_suggestions_open(es, matches, n, start, app->composer.cursor);
    } else {
        emoji_suggestions_close(es);
    }
}

/* Keys while the shortcode strip is open. Returns 1 when the key was used;
 * typing goes on to the input and the strip follows it. */
static int handle_emoji_suggestions(TuiApp *app, int is_key, int ch) {
    EmojiSuggestions *es = &app->emoji_suggestions;
    if (!es->count) return 0;
    if ((is_key && (ch == KEY_RIGHT || ch == KEY_DOWN)) || (!is_key && ch == '\t')) { emoji_suggestions_move(es, 1); return 1; }
    if (is_key && (ch == KEY_LEFT || ch == KEY_UP || ch == KEY_BTAB)) { emoji_suggestions_move(es, -1); return 1; }
    if (is_enter(is_key, ch)) { pick_emoji_suggestion(app, -1); return 1; }
    if (is_esc(is_key, ch)) { emoji_suggestions_dismiss(es); return 1; }
    return 0;
}

/* Up in an empty input, when the newest message in the chat is yours and
 * still editable, edits it (as in the desktop apps). Returns 1 when it did. */
static int edit_last_message(TuiApp *app) {
    if (!composer_view_is_empty(&app->composer) || app->editing_id[0] || app->attachment[0]) return 0;
    int count = 0;
    const Message *msgs = tui_app_messages(app, &count);
    if (count <= 0 || !msgs[count - 1].from_me || !messaging_manager_can_edit(app->deps.messaging, &msgs[count - 1])) return 0;
    tui_app_start_edit(app, count - 1);
    return 1;
}

/* An arrow key held with Ctrl, Alt or Shift, as terminals report it: Shift as its own key code,
 * Ctrl and the combinations as extended keys named kLFT5 and the like, Alt as Esc before the
 * arrow, and Option+arrow on a Mac as Esc b and Esc f. Returns which arrow (KEY_LEFT and so
 * on), or 0 for anything else. */
static int modified_arrow(int is_key, int ch, int alt) {
    if (alt && !is_key && (ch == 'b' || ch == 'f')) return ch == 'b' ? KEY_LEFT : KEY_RIGHT;
    if (!is_key) return 0;
    if (alt && (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN)) return ch;
    if (ch == KEY_SLEFT) return KEY_LEFT;
    if (ch == KEY_SRIGHT) return KEY_RIGHT;
    if (ch == KEY_SR) return KEY_UP;
    if (ch == KEY_SF) return KEY_DOWN;
    const char *name = keyname(ch);
    if (!name || name[0] != 'k' || strlen(name) != 5) return 0;         /* kLFT5, kRIT3, kUP5 is shorter */
    if (!strncmp(name, "kLFT", 4)) return KEY_LEFT;
    if (!strncmp(name, "kRIT", 4)) return KEY_RIGHT;
    return 0;
}

/* The same for the two shorter names, kUP5 and kDN5 and their kin. */
static int modified_vertical(int is_key, int ch) {
    const char *name = is_key ? keyname(ch) : NULL;
    if (!name || name[0] != 'k' || strlen(name) != 4) return 0;
    if (!strncmp(name, "kUP", 3)) return KEY_UP;
    if (!strncmp(name, "kDN", 3)) return KEY_DOWN;
    return 0;
}

static void handle_composer(TuiApp *app, int is_key, int ch, int alt) {
    const Settings *s = settings(app);
    /* With Ctrl, Alt or Shift held, the arrows step through what is typed a word at a time, and up and down go to its ends. */
    int arrow = modified_arrow(is_key, ch, alt);
    if (!arrow) arrow = modified_vertical(is_key, ch);
    if (arrow && !media_manager_is_recording(app->deps.media)) {
        if (arrow == KEY_LEFT || arrow == KEY_RIGHT) composer_view_move_word(&app->composer, arrow == KEY_LEFT ? -1 : 1);
        else composer_view_move_end(&app->composer, arrow == KEY_UP ? -1 : 1);
        app->dirty = 1;
        return;
    }
    if (media_manager_is_recording(app->deps.media)) {
        if (is_enter(is_key, ch)) tui_app_toggle_recording(app);
        else if (is_esc(is_key, ch)) {
            media_manager_cancel_recording(app->deps.media);
            messaging_manager_set_typing(app->deps.messaging, TYPING_PAUSED);
            tui_app_toast(app, "Recording discarded", 0);
        }
        return;
    }
    if (handle_emoji_suggestions(app, is_key, ch)) return;
    if (tui_app_mention_key(app, is_key, ch)) return;
    /* Command suggestions: Tab completes, ↑↓ choose, Enter runs. */
    int total = 0;
    const SlashCommand *all = tui_commands_all(&total);
    char *cmd = typed_command(app);
    if (cmd && command_suggestions_update(&app->suggestions, all, total, cmd) > 0) {
        const SlashCommand *sel = command_suggestions_selected(&app->suggestions, all);
        int done = 0;
        if (!is_key && ch == '\t') {
            char full[64];
            snprintf(full, sizeof(full), "/%s%s", sel->name, sel->args[0] ? " " : "");
            composer_view_set_text(&app->composer, full);
            done = 1;
        } else if (is_key && (ch == KEY_UP || ch == KEY_DOWN)) {
            command_suggestions_move(&app->suggestions, ch == KEY_UP ? -1 : 1);
            done = 1;
        } else if (is_enter(is_key, ch) && strcmp(cmd + 1, sel->name) != 0) {
            char full[64];
            snprintf(full, sizeof(full), "/%s", sel->name);
            composer_view_set_text(&app->composer, full);
        }
        free(cmd);
        if (done) return;
    } else {
        free(cmd);
        app->suggestions.count = 0;
    }

    if (is_esc(is_key, ch)) {
        if (app->editing_id[0]) tui_app_cancel_edit(app);
        else if (app->attachment[0]) { app->attachment[0] = '\0'; tui_app_toast(app, "Attachment removed", 0); }
        else if (app->reply.id[0]) tui_app_cancel_reply(app);
        else if (app->layout.sidebar.w) app->focus = TUI_FOCUS_CHATS;
        return;
    }
    if (!messaging_manager_open_jid(app->deps.messaging)[0]) {
        if (is_enter(is_key, ch) && app->layout.sidebar.w) app->focus = TUI_FOCUS_CHATS;
        return;
    }
    if (is_enter(is_key, ch) || (is_key && ch == TUI_KEY_NEWLINE)) {
        if (s->convert_emoticons) composer_view_convert_word(&app->composer, emoticon_to_emoji);
        if (is_key && ch == TUI_KEY_NEWLINE) composer_view_insert(&app->composer, L'\n');
        else if (!s->enter_sends) composer_view_insert(&app->composer, L'\n');
        else tui_app_send_composer(app);
        return;
    }
    if (!is_key && ch == CTRL('s')) { tui_app_send_composer(app); return; }
    if (is_ctrl_end(is_key, ch)) {                         /* the end of the input first, then the newest message */
        if (app->composer.cursor < app->composer.length) app->composer.cursor = app->composer.length;
        else tui_app_show_latest(app);
        return;
    }
    if (!is_key && ch == ' ' && s->convert_emoticons) composer_view_convert_word(&app->composer, emoticon_to_emoji);
    if (is_key && ch == KEY_UP && edit_last_message(app)) return;
    if (composer_view_key(&app->composer, is_key, ch)) {
        refresh_shortcode(app);
        tui_app_refresh_mention(app);
        if (!composer_view_is_empty(&app->composer) && app->composer.text[0] != L'/') {
            messaging_manager_set_typing(app->deps.messaging, TYPING_COMPOSING);
        }
        return;
    }
    if (is_key && ch == KEY_UP) {                      /* first line: go up to the messages */
        int count = 0;
        tui_app_messages(app, &count);
        app->focus = TUI_FOCUS_MESSAGES;
        message_view_select(&app->message_view, count, -1);
    }
}

static void handle_login(TuiApp *app, int is_key, int ch) {
    int action = login_view_key(&app->login, is_key, ch);
    if (action == LOGIN_ACTION_REQUEST_CODE) messaging_manager_request_pairing(app->deps.messaging, app->login.phone);
    else if (action == LOGIN_ACTION_NEW_QR && app->login.step == LOGIN_STEP_QR) messaging_manager_request_qr(app->deps.messaging);
}

void tui_input_dispatch(TuiApp *app, int is_key, int ch) {
    int alt = 0, shift = 0;
    if (is_key && ch == KEY_RESIZE) return;              /* layout is recomputed every frame */
    if (tui_app_locked(app)) {
        /* Locked: every key goes to the lock, and what the mouse or a paste sends is thrown away unread. */
        if (is_key && ch == KEY_MOUSE) { MEVENT gone; getmouse(&gone); return; }
        if (!is_key && ch == 27) {
            if (escape_sequence_read().kind == ESCAPE_SEQUENCE_PASTE) free(read_paste_body());
            return;
        }
        tui_app_lock_key(app, is_key, ch);
        return;
    }
    if (is_key && ch == KEY_MOUSE) { handle_mouse(app); return; }
    if (!is_key && ch == 27) {
        EscapeSequence seq = escape_sequence_read();
        if (seq.kind == ESCAPE_SEQUENCE_PASTE) {
            char *text = read_paste_body();
            if (text) { handle_paste(app, text); free(text); }
            return;
        }
        if (seq.kind == ESCAPE_SEQUENCE_KEY) {
            int ctrl = seq.mods & ESCAPE_MOD_CTRL;
            shift = shift_key(seq.mods);
            if (ctrl && shift && (seq.code == 'l' || seq.code == 'L') && !tui_app_show_login(app)) {
                tui_app_toggle_soft_lock_here(app);
                return;
            }
            /* Any other modified key goes on as the key curses would have seen. */
            is_key = 0;
            ch = ctrl && seq.code < 128 && isalpha(seq.code) ? (tolower(seq.code) & 0x1f) : seq.code;
            alt = (seq.mods & ESCAPE_MOD_ALT) != 0;
        } else {
            /* ESC followed immediately by a key is Alt+key. */
            wint_t next;
            wtimeout(stdscr, 0);
            int rc = wget_wch(stdscr, &next);
            wtimeout(stdscr, 100);
            if (rc != ERR) {
                alt = 1;
                is_key = rc == KEY_CODE_YES;
                ch = (int)next;
            }
        }
    }
    if (!is_key && !alt && platform_is_macos()) {       /* Option+letter typed a character */
        int letter = mac_option_letter(ch, typing_text(app));
        if (letter) { alt = 1; ch = letter; }
    }
    if ((alt || shift) && is_enter(is_key, ch)) {       /* a line break, wherever text is typed */
        is_key = 1;
        ch = TUI_KEY_NEWLINE;
        alt = 0;
    }
    if (!is_key && (ch == CTRL('q') || ch == CTRL('c'))) { tui_app_quit(app); return; }
    if (app->status_composer.open && !is_key && ((alt && (ch == 'v' || ch == 'V')) || ch == CTRL('v'))) {
        tui_app_status_paste_picture(app);                  /* the clipboard's picture as the status */
        return;
    }
    if (handle_overlays_key(app, is_key, ch)) return;

    ConnectionHealth health;
    messaging_manager_health(app->deps.messaging, &health);
    if (health.show_overlay) {
        if (!is_key && (ch == 'r' || ch == 'R')) messaging_manager_retry_now(app->deps.messaging);
        else if (!is_key && (ch == 'q' || ch == 'Q')) tui_app_quit(app);
        else if (is_key && ch == KEY_F(2)) settings_panel_open(&app->settings_panel);
        return;
    }
    if (alt && !is_key && (ch == 'v' || ch == 'V') && !tui_app_show_login(app)) { tui_app_paste_image(app); return; }
    if (alt && !is_key && (ch == 'l' || ch == 'L') && !tui_app_show_login(app)) { tui_app_toggle_soft_lock_here(app); return; }
    if (alt && !is_key && (ch == 't' || ch == 'T') && !tui_app_show_login(app)) {
        tui_app_step_show_transcripts(app, messaging_manager_open_jid(app->deps.messaging));
        return;
    }
    if (alt && !is_key && (ch == 'a' || ch == 'A') && app->focus != TUI_FOCUS_CHATS && !tui_app_show_login(app)) {
        if (ch == 'A') tui_app_keep_send_account(app);      /* with Shift: for this contact from now on */
        else tui_app_cycle_send_account(app);
        return;
    }
    if (alt && !is_key && (ch == 'i' || ch == 'I') && !tui_app_show_login(app)) {
        const char *jid = messaging_manager_open_jid(app->deps.messaging);
        if (app->focus == TUI_FOCUS_CHATS) {
            const char *sel = tui_app_take_selected(app);
            if (sel) jid = sel;
        }
        char copy[128];
        str_copy(copy, sizeof(copy), jid);
        tui_app_open_contact(app, copy);
        return;
    }
    if (handle_global(app, is_key, ch)) return;
    if (tui_app_show_login(app)) { handle_login(app, is_key, ch); return; }
    switch (app->focus) {
        case TUI_FOCUS_CHATS:    handle_chats(app, is_key, ch, alt); break;
        case TUI_FOCUS_MESSAGES: handle_messages(app, is_key, ch, alt); break;
        case TUI_FOCUS_COMPOSER: handle_composer(app, is_key, ch, alt); break;
    }
}
