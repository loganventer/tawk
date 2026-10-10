#include "core/icon_glyphs.h"
#include "tui_app_state.h"

#include "clients/tui/chat_subtitle.h"
#include "clients/tui/footer_bar.h"
#include "clients/tui/header_bar.h"
#include "clients/tui/outage_overlay.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "core/unread_tally.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define DOT "\xC2\xB7"

/* Short hints for the part of the screen that has focus (right-aligned). */
static const char *hints(TuiApp *app) {
    if (app->focus == TUI_FOCUS_CHATS) {
        if (app->chat_list.filtering) return "Enter open " DOT " Esc clear";
        return app->chat_list.folder != CHAT_FOLDER_CHATS ? "type to search " DOT " Enter open " DOT " Esc back"
                                                          : "type to search " DOT " Enter open " DOT " Alt+O options " DOT " F2";
    }
    if (app->focus == TUI_FOCUS_MESSAGES) return "Alt+Q reply " DOT " Alt+E react " DOT " Alt+M menu " DOT " type to write";
    return "Enter send " DOT " / commands " DOT " Ctrl+R voice " DOT " F2";
}

/* Connection state as a single emoji. */
static const char *status_emoji(AuthState auth) {
    switch (auth) {
        case AUTH_STATE_CONNECTED:    return "\xF0\x9F\x9F\xA2";   /* 🟢 */
        case AUTH_STATE_NEEDS_LOGIN:  return "\xE2\x9A\xAA";        /* ⚪ */
        case AUTH_STATE_FAILED:       return "\xF0\x9F\x94\xB4";   /* 🔴 */
        default:                      return "\xF0\x9F\x9F\xA1";   /* 🟡 connecting or reconnecting */
    }
}

/* Pixel images only while nothing floats over the conversation: curses
 * cannot draw text on top of them. */
static int pixel_mode(TuiApp *app, const Settings *s) {
    if (strcmp(s->image_mode, "blocks") == 0) return 0;
    return strcmp(s->image_mode, "sixel") == 0 || app->deps.sixel_supported;
}

/* `beside_contact`: the contact panel may be open, for pictures it never
 * covers (portraits in the title bar and the chat list). */
static int use_pixel_images(TuiApp *app, const Settings *s, int beside_contact) {
    if (!s->inline_thumbnails || !pixel_mode(app, s) || app->viewer.open) return 0;
    ConnectionHealth health;
    messaging_manager_health(app->deps.messaging, &health);
    int typing_command = app->composer.length && app->composer.text[0] == L'/';
    return !((app->contact.open && !beside_contact) || app->reader.open || app->theme_picker.open || app->search.open || app->message_menu.open ||
             app->emoji_picker.open || app->options.open || app->attach_menu.open || app->camera_view.open || app->message_info.open || app->palette.open ||
             app->file_picker.open || app->settings_panel.open || profile_dialogs_is_open(&app->profile) ||
             app->status_composer.open || status_feed_dialogs_is_open(&app->feed) || app->forward_picker.open || app->self_chats.open || app->voice_languages.open || app->scheduled_list.open || app->agents.open ||
             app->accounts_dialog.open || app->send_accounts.open || health.show_overlay || typing_command);
}

/* The status line above the input: an attachment or the message being answered. */
static void composer_chip(TuiApp *app, char *out, size_t size) {
    out[0] = '\0';
    if (app->editing_id[0]) {
        snprintf(out, size, "\xE2\x9C\x8F Editing your message " DOT " Enter save " DOT " Esc cancel");
    } else if (app->attachment[0]) {
        struct stat st;
        const char *slash = strrchr(app->attachment, '/');
        double mb = stat(app->attachment, &st) == 0 ? (double)st.st_size / (1024.0 * 1024.0) : 0;
        snprintf(out, size, ICON_FILE " %.200s (%.1f MB) " DOT " Enter send " DOT " Esc remove",
                 slash ? slash + 1 : app->attachment, mb);
    } else if (app->reply.id[0]) {
        snprintf(out, size, "\xE2\x86\xA9 Replying to %.60s: \"%.80s\" " DOT " Esc cancel", app->reply_name, app->reply.text);
    }
}

void tui_render_frame(TuiApp *app, int64_t now) {
    if (tui_app_locked(app)) {                              /* nothing of tawk is drawn behind the lock */
        int lock_rows, lock_cols;
        getmaxyx(stdscr, lock_rows, lock_cols);
        lock_screen_draw(&app->lock, lock_rows, lock_cols, now);
        return;
    }
    const Settings *s = settings_manager_current(app->deps.settings);
    MessagingManager *mm = app->deps.messaging;
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int width = app->dragging_divider ? app->drag_width : s->sidebar_width;

    /* The input grows with its text: measure it at the width it will get. */
    tui_layout_compute(&app->layout, rows, cols, width, s->sidebar_collapsed, 1);
    int needed = composer_view_rows_needed(&app->composer, app->layout.composer.w, 1);
    tui_layout_compute(&app->layout, rows, cols, width, s->sidebar_collapsed, needed);
    TuiLayout *l = &app->layout;
    if (!l->sidebar.w && app->focus == TUI_FOCUS_CHATS) app->focus = TUI_FOCUS_COMPOSER;

    bkgdset(tui_palette_attr(THEME_SLOT_BASE));
    erase();
    if (l->header.h == 0) {
        tui_text_center(rows / 2, 0, cols, "Window too small", ATTR_BOLD);
        refresh();
        return;
    }

    char tally[160];
    unread_tally_format(tui_app_tally(app), tally, sizeof(tally));
    char account_chip[ACCOUNT_LABEL_SIZE + 8];
    tui_app_account_chip(app, account_chip, sizeof(account_chip));
    AuthState auth = messaging_manager_auth_state(mm);
    HeaderModel header = {
        .user_name = tui_app_user_name(app),
        .status = status_emoji(auth),
        .dnd = s->do_not_disturb,
        .tally = tally,
        .blink_on = blink_state_on(app->deps.blink, NULL, now),
        .use_24h = s->use_24h_clock,
        .sidebar_open = l->sidebar.w > 0,
        .show_post = !tui_app_show_login(app),
        .unseen_statuses = tui_app_statuses_unseen(app),
        .show_tabs = !tui_app_show_login(app),
        .agents_tab_active = app->agents.open,
        .agents_waiting = approval_queue_count(app->deps.approvals),
        .agents_high = approval_queue_high_count(app->deps.approvals),
        .agents_connected = app->deps.automation ? automation_manager_status(app->deps.automation)->session_count : 0,
        .account = account_chip,
    };
    header_bar_render(l->header, &header, &app->header_hits);

    ConnectionHealth health;
    messaging_manager_health(mm, &health);
    int login = tui_app_show_login(app);
    int pixels = !login && use_pixel_images(app, s, 0);
    int portrait_pixels = !login && use_pixel_images(app, s, 1);
    int n_msgs = 0;
    const Message *msgs = NULL;

    if (login) {
        login_view_render(&app->login, l->body, messaging_manager_qr(mm), messaging_manager_pairing_code(mm),
                          messaging_manager_user_name(mm), app->toast_error ? app->toast : "");
    } else {
        int count = 0;
        const Chat *chats = tui_app_chat_rows(app, &count);
        tui_app_narrow_rows(app, chats, count);
        if (l->sidebar.w) {
            app->chat_list.compact = strcmp(s->chat_list_style, "compact") == 0;
            app->chat_list.spacing = s->chat_spacing;
            app->chat_list.portraits = s->portraits ? &app->portraits : NULL;
            app->chat_list.thumbs = app->thumbs;
            app->chat_list.pixel_images = portrait_pixels;
            chat_list_view_render(&app->chat_list, l->sidebar, chats, count, app->focus == TUI_FOCUS_CHATS,
                                  s->use_24h_clock, app->deps.blink, now);
            tui_vline(l->divider.y, l->divider.x, l->divider.h,
                      tui_palette_attr(app->dragging_divider ? THEME_SLOT_ACCENT : THEME_SLOT_BORDER));
        }
        const Chat *chat = messaging_manager_open_chat_info(mm);
        msgs = tui_app_messages(app, &n_msgs);
        char title[192] = "";
        if (chat) snprintf(title, sizeof(title), "%s%s%s", chat->soft_locked ? "\xF0\x9F\x99\x88 " : "", chat->name,
                           chat->is_muted ? "  \xF0\x9F\x94\x95" : "");
        char subtitle[160];
        chat_subtitle_text(s, chat, chat ? profile_manager_summary(app->deps.profiles, chat->jid) : "", (int64_t)time(NULL), subtitle, sizeof(subtitle));
        MessageViewContext ctx = {
            .title = title,
            .status = messaging_manager_history_pending(mm) ? "\xE2\x9F\xB3 loading older messages\xE2\x80\xA6" : "",
            .is_group = chat ? chat->is_group : 0,
            .focused = app->focus == TUI_FOCUS_MESSAGES,
            .use_24h = s->use_24h_clock,
            .playing_path = media_manager_playing(app->deps.media),
            .playing_ms = media_manager_playing_ms(app->deps.media),
            .names = tui_app_names(app),
            .thumbs = s->inline_thumbnails ? app->thumbs : NULL,
            .media = &app->media_sources,
            .pixel_images = pixels,
            .portrait_pixels = portrait_pixels,
            .veiled = chat && chat->soft_locked,
            .jid = chat && s->portraits ? chat->jid : NULL,
            .portrait = chat && s->portraits && !chat->soft_locked ? profile_manager_picture(app->deps.profiles, chat->jid) : NULL,
            .subtitle = subtitle,
            .activity = chat && !chat->soft_locked ? chat->typing : "",
            .activity_phase = (int)(now / 300),
            .formatter = &app->formatter,
            .statuses = &app->status_source,
            .transcripts = tui_app_transcripts_for(app, chat),
            .summaries = tui_app_summaries_for(app, chat),
        };
        ScheduledMessage *scheduled = NULL;
        int n_scheduled = 0;
        if (chat && app->deps.scheduling) scheduling_manager_list(app->deps.scheduling, chat->jid, &scheduled, &n_scheduled);
        ctx.scheduled = scheduled;
        ctx.scheduled_count = n_scheduled;
        app->message_view.has_newer = messaging_manager_has_newer(mm);
        ctx.owners = app->peer_count ? app->merged.owners : NULL;
        ctx.badges = app->chat_list.badges;
        ctx.badge_count = app->peer_count ? app->chat_list.badge_count : 0;
        message_view_render(&app->message_view, l->chat, msgs, n_msgs, &ctx);
        scheduled_message_array_free(scheduled, n_scheduled);

        char chip[400];
        composer_chip(app, chip, sizeof(chip));
        int recording = media_manager_is_recording(app->deps.media) ? media_manager_recording_seconds(app->deps.media) : -1;
        tui_app_send_label(app, app->composer.sending_as, sizeof(app->composer.sending_as));
        composer_view_render(&app->composer, l->composer, app->focus == TUI_FOCUS_COMPOSER,
                             chat != NULL, recording, s->enter_sends, chip);

        /* /command suggestions float just above the input. */
        int total = 0;
        const SlashCommand *all = tui_commands_all(&total);
        char *typed = app->composer.length && app->composer.text[0] == L'/' ? composer_view_text(&app->composer) : NULL;
        if (typed && app->focus == TUI_FOCUS_COMPOSER &&
            command_suggestions_update(&app->suggestions, all, total, typed) > 0) {
            command_suggestions_render(&app->suggestions, all, l->composer);
        } else {
            app->suggestions.count = 0;
        }
        free(typed);
        if (app->focus == TUI_FOCUS_COMPOSER) {
            emoji_suggestions_render(&app->emoji_suggestions, app->deps.emoji, l->composer);
            mention_suggestions_render(&app->mention_suggestions, l->composer);
        } else {
            emoji_suggestions_close(&app->emoji_suggestions);
            mention_suggestions_close(&app->mention_suggestions);
        }
    }

    footer_bar_render(l->footer, login ? "" : hints(app), app->toast, app->toast_error);

    /* Popups, topmost last. */
    int popup = 1;
    ImagePlacement viewer_image;
    int viewer_pixels = 0;
    UiRect center = { l->body.y, l->body.x + (l->body.w > 104 ? (l->body.w - 100) / 2 : 1), l->body.h,
                      l->body.w > 104 ? 100 : l->body.w - 2 };
    ImagePlacement contact_image;
    int contact_pixels = 0;
    const char *profile_jid = image_viewer_profile_jid(&app->viewer);  /* keep the sharpest profile picture showing */
    if (profile_jid) {
        const char *full = profile_manager_full_picture(app->deps.profiles, profile_jid);
        if (full) str_copy(app->viewer.portrait_path, sizeof(app->viewer.portrait_path), full);
    }
    if (app->viewer.open && !login) {
        viewer_pixels = image_viewer_render(&app->viewer, l->body, msgs, n_msgs, app->thumbs, tui_app_names(app),
                                            s->use_24h_clock, pixel_mode(app, s), &viewer_image);
    } else if (app->contact.open && !login) {
        const Chat *who = NULL;
        int n = 0;
        const Chat *all = messaging_manager_chats(mm, &n);
        for (int i = 0; i < n; i++) if (strcmp(all[i].jid, app->contact.jid) == 0) who = &all[i];
        if (!who) {
            app->contact.open = 0;                                     /* the chat went away */
        } else {
            ContactProfile profile;
            int known = profile_manager_details(app->deps.profiles, who->jid, 0, &profile) == 0;
            contact_pixels = contact_panel_render(&app->contact, l->chat, who, known ? &profile : NULL,
                                                  profile_manager_picture(app->deps.profiles, who->jid), tui_app_member_name, app,
                                                  app->thumbs, pixel_mode(app, s), &contact_image);
            contact_profile_dispose(&profile);
        }
    } else if (app->reader.open) {
        text_reader_render(&app->reader, center);
    } else if (app->theme_picker.open) {
        /* Over the chat list, so the conversation stays visible for the preview. */
        UiRect side = l->sidebar.w ? l->sidebar : (UiRect){ l->body.y, l->body.x, l->body.h, l->body.w / 2 };
        theme_picker_overlay_render(&app->theme_picker, side, settings_manager_themes(app->deps.settings));
    } else if (app->search.open) {
        search_overlay_render(&app->search, center, tui_app_names(app), &app->formatter, s->use_24h_clock);
    } else if (app->message_menu.open) {
        message_menu_render(&app->message_menu, l->body);
    } else if (app->agents.open) {
        tui_app_agents_render(app, l->body);
    } else if (app->send_accounts.open) {
        tui_app_send_accounts_render(app, l->body);
    } else if (app->accounts_dialog.open) {
        tui_app_accounts_render(app, l->body);
    } else if (app->scheduled_list.open) {
        tui_app_scheduled_render(app, l->body);
    } else if (app->voice_languages.open) {
        tui_app_voice_languages_render(app, l->body);
    } else if (app->self_chats.open) {
        int n = 0;
        const Chat *all = messaging_manager_chats(mm, &n);
        chat_toggle_dialog_render(&app->self_chats, l->body, all, n);
    } else if (app->forward_picker.open) {
        int n = 0;
        const Chat *all = messaging_manager_chats(mm, &n);
        chat_picker_render(&app->forward_picker, l->body, all, n);
    } else if (app->emoji_picker.open) {
        emoji_picker_render(&app->emoji_picker, app->emoji_picker.purpose == EMOJI_PICKER_FOR_INPUT ?
                            (UiRect){ l->chat.y, l->chat.x, l->chat.h + l->composer.h, l->chat.w } : l->chat,
                            app->deps.emoji);
    } else if (app->options.open) {
        chat_options_menu_render(&app->options, l->body);
    } else if (app->attach_menu.open) {
        attach_menu_render(&app->attach_menu, l->chat);
    } else if (app->camera_view.open) {
        camera_view_render(&app->camera_view, l->chat, pixel_mode(app, s));
    } else if (app->message_info.open) {
        Receipt receipts[256];
        int count = messaging_manager_message_receipts(app->deps.messaging, app->message_info.message_id, receipts, 256);
        message_info_panel_render(&app->message_info, l->body, receipts, count, s->use_24h_clock);
    } else if (app->palette.open) {
        reaction_palette_render(&app->palette, l->chat);
    } else if (app->file_picker.open) {
        file_picker_render(&app->file_picker, center);
    } else if (profile_dialogs_is_open(&app->profile) && !login) {
        ProfileViewModel model;
        char about[700];
        tui_app_profile_model(app, &model, about, sizeof(about));
        profile_dialogs_render(&app->profile, l->body, &model, app->thumbs);
    } else if (status_feed_dialogs_is_open(&app->feed) && !login) {
        tui_app_statuses_render(app, l->body);
    } else if (app->status_composer.open && !login) {
        status_composer_dialog_render(&app->status_composer, l->body,
                                      status_manager_background_name(app->deps.statuses, app->status_composer.background),
                                      status_manager_background(app->deps.statuses, app->status_composer.background),
                                      media_manager_camera_available(app->deps.media));
    } else if (app->settings_panel.open) {
        UiRect area = { l->body.y, l->body.x + (l->body.w > 100 ? (l->body.w - 96) / 2 : 1), l->body.h,
                        l->body.w > 100 ? 96 : l->body.w - 2 };
        settings_panel_render(&app->settings_panel, area, now);
    } else if (health.show_overlay) {
        outage_overlay_render(l->body, &health);
    } else {
        popup = 0;
    }

    /* A confirmation sits above everything, including settings; a ringing call above that. */
    if (app->confirm.open) {
        confirm_dialog_render(&app->confirm, l->body, now);
        popup = 1;
    }
    const IncomingCall *call = login ? NULL : call_manager_ringing(app->deps.calls);
    ImagePlacement call_image;
    int call_pixels = 0;
    if (call) {
        char name[128];
        messaging_manager_display_name(mm, call->from, name, sizeof(name));
        call_pixels = incoming_call_render(&app->call_view, l->body, call->from, name,
                                           profile_manager_picture(app->deps.profiles, call->from), app->thumbs,
                                           pixel_mode(app, s), clock_now_ms() - call->since_ms, &call_image);
        popup = 1;
    }

    /* The blinking cursor goes to the field that takes typing: the topmost
     * popup's field, else the chat list search, else the input. Only touch
     * the terminal's cursor state when it actually changes (no flicker). */
    TextCaret where = { 0, 0, 0 };
    if (call || app->confirm.open || app->contact.open || app->viewer.open || app->reader.open || app->theme_picker.open || app->message_menu.open ||
        app->options.open || app->attach_menu.open || app->camera_view.open || app->message_info.open || app->palette.open ||
        app->file_picker.open) {
        where.visible = 0;
    } else if (app->agents.open) {
        where = app->agents.caret;
    } else if (app->accounts_dialog.open) {
        where = app->accounts_dialog.caret;
    } else if (app->voice_languages.open) {
        where = app->voice_languages.caret;
    } else if (app->self_chats.open) {
        where = app->self_chats.caret;
    } else if (app->forward_picker.open) {
        where = app->forward_picker.caret;
    } else if (app->scheduled_list.open) {
        where = app->scheduled_list.caret;
    } else if (profile_dialogs_is_open(&app->profile)) {
        where = app->profile.caret;
    } else if (status_feed_dialogs_is_open(&app->feed)) {
        where = app->feed.viewer.caret;                          /* typing a reply to a status */
        where.visible = where.visible && status_feed_dialogs_replying(&app->feed);
    } else if (app->status_composer.open) {
        where = app->status_composer.caret;
    } else if (app->search.open) {
        where = app->search.caret;
    } else if (app->emoji_picker.open) {
        where = app->emoji_picker.caret;
    } else if (app->settings_panel.open) {
        where = app->settings_panel.caret;
        where.visible = where.visible && app->settings_panel.editing;
    } else if (health.show_overlay) {
        where.visible = 0;
    } else if (login) {
        where = app->login.caret;
        where.visible = where.visible && app->login.step == LOGIN_STEP_PHONE_ENTRY;
    } else if (app->focus == TUI_FOCUS_CHATS && app->chat_list.filtering) {
        where = app->chat_list.caret;
    } else if (app->focus == TUI_FOCUS_COMPOSER && app->composer.caret_y >= 0) {
        where = (TextCaret){ 1, app->composer.caret_y, app->composer.caret_x };
    }
    (void)popup;
    int caret = where.visible;
    if (caret) move(where.y, where.x);
    if (caret != app->caret_visible) {
        curs_set(caret ? 1 : 0);
        app->caret_visible = caret;
    }
    /* Always through the overlay, so images are cleared when they go away. */
    if (call) {
        sixel_overlay_present(&app->sixel_overlay, app->sixels, app->deps.cell_width_px, app->deps.cell_height_px,
                              &call_image, call_pixels, msgs, n_msgs);
    } else if (app->contact.open && !app->viewer.open && !app->confirm.open) {
        /* The panel's picture, and the portraits it leaves uncovered. */
        ImagePlacement all[SIXEL_OVERLAY_MAX];
        int n = 0;
        if (contact_pixels) all[n++] = contact_image;
        for (int i = 0; portrait_pixels && i < app->message_view.placement_count && n < SIXEL_OVERLAY_MAX; i++) {
            if (app->message_view.placements[i].message < 0) all[n++] = app->message_view.placements[i];
        }
        for (int i = 0; portrait_pixels && l->sidebar.w && i < app->chat_list.placement_count && n < SIXEL_OVERLAY_MAX; i++) all[n++] = app->chat_list.placements[i];
        sixel_overlay_present(&app->sixel_overlay, app->sixels,
                              app->deps.cell_width_px, app->deps.cell_height_px, all, n, msgs, n_msgs);
    } else if (app->viewer.open) {
        sixel_overlay_present(&app->sixel_overlay, app->sixels, app->deps.cell_width_px, app->deps.cell_height_px,
                              &viewer_image, viewer_pixels, msgs, n_msgs);
    } else {
        /* The conversation's pictures and portraits, and the chat list's portraits. */
        ImagePlacement all[SIXEL_OVERLAY_MAX];
        int n = 0;
        for (int i = 0; pixels && i < app->message_view.placement_count && n < SIXEL_OVERLAY_MAX; i++) all[n++] = app->message_view.placements[i];
        for (int i = 0; pixels && l->sidebar.w && i < app->chat_list.placement_count && n < SIXEL_OVERLAY_MAX; i++) all[n++] = app->chat_list.placements[i];
        sixel_overlay_present(&app->sixel_overlay, app->sixels, app->deps.cell_width_px, app->deps.cell_height_px,
                              all, n, msgs, n_msgs);
    }
    if (app->camera_view.open) camera_view_present_pixels(&app->camera_view, app->deps.cell_width_px, app->deps.cell_height_px);
}
