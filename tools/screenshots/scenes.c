/* Draws tawk's screens for the manual with made-up chats, profiles and
 * statuses, using the same drawing code as the app, and writes each screen
 * as a grid of cells (character, colours, bold) for render.py to paint.
 *
 *   scenes OUT_DIR PICTURE_DIR
 *
 * PICTURE_DIR holds avatar.png, landscape.png and webcam.png (render.py
 * makes them). */
#include "clients/tui/account_badge.h"
#include "clients/tui/accounts_dialog.h"
#include "clients/tui/agents_panel.h"
#include "clients/tui/approval_queue.h"
#include "clients/tui/attach_menu.h"
#include "clients/tui/camera_view.h"
#include "clients/tui/chat_list_view.h"
#include "clients/tui/chat_picker.h"
#include "clients/tui/chat_toggle_dialog.h"
#include "clients/tui/composer_view.h"
#include "clients/tui/confirm_dialog.h"
#include "clients/tui/footer_bar.h"
#include "clients/tui/header_bar.h"
#include "clients/tui/mention_suggestions.h"
#include "clients/tui/message_view.h"
#include "clients/tui/profile_dialogs.h"
#include "clients/tui/scheduled_list_dialog.h"
#include "clients/tui/settings_panel.h"
#include "clients/tui/splash_view.h"
#include "clients/tui/status_composer_dialog.h"
#include "clients/tui/status_feed_dialogs.h"
#include "clients/tui/thumbnail_cache.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "resource_access/json_theme_repository.h"
#include "core/mention_list.h"
#include "core/mention_name.h"
#include "core/settings.h"
#include "engines/whatsapp_markup.h"
#include "utilities/app_info.h"
#include "utilities/rgb_image_file.h"
#include "utilities/str_util.h"

#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#define ROWS 42
#define COLS 140
#define SIDEBAR 35

/* The footer shows the keys for whatever has focus, as in the app. */
#define LIST_HINTS  "type to search \xC2\xB7 Enter open \xC2\xB7 Alt+O options \xC2\xB7 F2"
#define INPUT_HINTS "Enter send \xC2\xB7 / commands \xC2\xB7 Ctrl+R voice \xC2\xB7 F2"

static char s_out[256], s_pics[256];
static ThumbnailCache *s_thumbs;

static void path_in(char *out, size_t size, const char *dir, const char *name) { snprintf(out, size, "%s/%s", dir, name); }

/* ---- the app behind the dialogs ----------------------------------------- */

static Chat s_chats[7];
static int  s_chat_count;

static void add_chat(const char *jid, const char *name, const char *preview, int minutes_ago, int unread, int pinned) {
    Chat *c = &s_chats[s_chat_count++];
    chat_init(c, jid);
    str_copy(c->name, sizeof(c->name), name);
    str_copy(c->preview, sizeof(c->preview), preview);
    c->last_ts = (int64_t)time(NULL) - minutes_ago * 60;
    c->unread = unread;
    c->is_pinned = pinned;
    c->is_group = chat_jid_is_group(jid);
    c->is_archived = c->is_locked = 0;
}

static void make_chats(void) {
    add_chat("27820000001@s.whatsapp.net", "Mom", "Isn't it beautiful? \xF0\x9F\x98\x8D", 12, 0, 1);
    add_chat("27820000002@s.whatsapp.net", "Sarah", "We have to go back there", 55, 0, 1);
    add_chat("120363000000000001@g.us", "Dev team", "Staging looks good from here", 80, 2, 0);
    add_chat("27820000003@s.whatsapp.net", "Pieter", "\xF0\x9F\x8E\xAC Video", 200, 0, 0);
    add_chat("120363000000000002@g.us", "Book club", "Chapter 12 was wild", 60 * 20, 0, 0);
    add_chat("27820000004@s.whatsapp.net", "Nadia", "Thanks for the book recommendation", 60 * 48, 0, 0);
    qsort(s_chats, (size_t)s_chat_count, sizeof(Chat), chat_compare);
}

/* With several accounts: the header's account chip and the badges on the rows. The accounts scenes set these. */
static const char  *s_account_chip = "";
static AccountBadge s_badges[ACCOUNT_MAX];
static int          s_badge_count;

/* What the header says about agents: the Agents scenes set these. */
static int s_agents_waiting, s_agents_high, s_agents_connected, s_agents_tab;

/* Chats have no pictures here, so the list shows their initials badges. */
static const char *no_picture(void *ctx, const char *jid) { (void)ctx; (void)jid; return NULL; }

/* Header, chat list, an empty conversation and the footer. */
static void draw_app(const char *footer) {
    int64_t now = (int64_t)time(NULL) * 1000;
    UiRect screen = { 0, 0, ROWS, COLS };
    tui_fill(screen, tui_palette_attr(THEME_SLOT_BASE));
    HeaderModel m = { .user_name = "Alex", .status = "\xF0\x9F\x9F\xA2", .tally = "\xF0\x9F\x92\xAC 2", .use_24h = 1,
                      .sidebar_open = 1, .show_post = 1, .unseen_statuses = 3,
                      .show_tabs = 1, .agents_tab_active = s_agents_tab, .agents_waiting = s_agents_waiting,
                      .agents_high = s_agents_high, .agents_connected = s_agents_connected, .account = s_account_chip };
    HeaderHits hits;
    header_bar_render((UiRect){ 0, 0, 1, COLS }, &m, &hits);
    static ChatListView list;
    chat_list_view_init(&list);
    static const PortraitSource portraits = { NULL, no_picture };
    list.portraits = &portraits;
    list.thumbs = s_thumbs;
    list.spacing = 1;
    memcpy(list.badges, s_badges, sizeof(list.badges));
    list.badge_count = s_badge_count;
    BlinkState blink = { "", ACCOUNT_ID_NONE, 0 };
    chat_list_view_render(&list, (UiRect){ 1, 0, ROWS - 2, SIDEBAR - 1 }, s_chats, s_chat_count, 0, 1, &blink, now);
    tui_vline(1, SIDEBAR - 1, ROWS - 2, tui_palette_attr(THEME_SLOT_BORDER));
    tui_fill((UiRect){ 1, SIDEBAR, ROWS - 2, COLS - SIDEBAR }, tui_palette_attr(THEME_SLOT_CHAT));
    tui_text(2, SIDEBAR + 1, 40, "Select a chat", tui_palette_attr(THEME_SLOT_CHAT) | ATTR_DIM);
    footer_bar_render((UiRect){ ROWS - 1, 0, 1, COLS }, footer, "", 0);
}

static UiRect body(void) { return (UiRect){ 1, 0, ROWS - 2, COLS }; }

/* ---- writing a screen --------------------------------------------------- */

static void pair_colours(int pair, int *fg, int *bg) {
    int f = -1, b = -1;
    extended_pair_content(pair, &f, &b);
    int bf = -1, bb = -1;
    extended_pair_content(PAIR_NUMBER(tui_palette_attr(THEME_SLOT_BASE)), &bf, &bb);
    *fg = f < 0 ? bf : f;
    *bg = b < 0 ? bb : b;
}

/* One line per row; each cell is "codepoint,fg,bg,flags" with flags
 * 1 bold, 2 dim, 4 reverse, 8 wide. Wide characters take two cells; the
 * second is written as 0. */
static void save(const char *name) {
    refresh();
    char path[700];
    snprintf(path, sizeof(path), "%s/%s.cells", s_out, name);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    fprintf(f, "%d %d\n", ROWS, COLS);
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            cchar_t cc;
            wchar_t wch[CCHARW_MAX + 1] = { 0 };
            attr_t attrs = 0;
            short pair_short = 0;
            int pair = 0;
            mvin_wch(y, x, &cc);
            getcchar(&cc, wch, &attrs, &pair_short, &pair);
            if (!pair) pair = pair_short;
            int fg, bg;
            pair_colours(pair, &fg, &bg);
            int w = wcwidth(wch[0]);
            int flags = ((attrs & A_BOLD) ? 1 : 0) | ((attrs & A_DIM) ? 2 : 0) | ((attrs & A_REVERSE) ? 4 : 0) | (w == 2 ? 8 : 0);
            fprintf(f, "%s%u,%d,%d,%d", x ? " " : "", (unsigned)(wch[0] ? wch[0] : L' '), fg, bg, flags);
            if (w == 2 && x + 1 < COLS) { fprintf(f, " 0,%d,%d,0", fg, bg); x++; }
        }
        fputc('\n', f);
    }
    fclose(f);
    printf("  %s\n", name);
}

/* ---- scenes ------------------------------------------------------------- */

static void profile_scenes(void) {
    char avatar[700];
    path_in(avatar, sizeof(avatar), s_pics, "avatar.png");
    ProfileViewModel m = { "27821234567@s.whatsapp.net", "Alex", "Out hiking \xF0\x9F\xA5\xBE back on Monday", avatar, { 0, 0, 0 } };
    ProfileDialogs d;

    profile_dialogs_open(&d);
    draw_app(LIST_HINTS);
    profile_dialogs_render(&d, body(), &m, s_thumbs);
    save("profile");

    profile_dialogs_key(&d, 1, KEY_DOWN, 1, 1);
    profile_dialogs_key(&d, 0, '\n', 1, 1);
    profile_dialogs_edit_text(&d, m.about, 139);
    draw_app(LIST_HINTS);
    profile_dialogs_render(&d, body(), &m, s_thumbs);
    save("profile-edit");

    profile_dialogs_key(&d, 0, 27, 1, 1);
    profile_dialogs_key(&d, 1, KEY_DOWN, 1, 1);
    profile_dialogs_key(&d, 0, '\n', 1, 1);
    draw_app(LIST_HINTS);
    profile_dialogs_render(&d, body(), &m, s_thumbs);
    save("profile-photo");
}

static StatusAuthor author(const char *jid, const char *name, int count, int unviewed, int minutes_ago, int mine) {
    StatusAuthor a;
    memset(&a, 0, sizeof(a));
    str_copy(a.jid, sizeof(a.jid), jid);
    str_copy(a.name, sizeof(a.name), name);
    a.count = count;
    a.unviewed = unviewed;
    a.latest = (int64_t)time(NULL) - minutes_ago * 60;
    a.from_me = mine;
    return a;
}

static void viewer_name(void *ctx, const char *jid, char *out, size_t size) {
    (void)ctx;
    static const char *const NAMES[] = { "Mom", "Sarah", "Pieter", "Nadia", "Lindiwe" };
    int k = jid[10] - '1';
    str_copy(out, size, k >= 0 && k < 5 ? NAMES[k] : jid);
}

static void status_scenes(void) {
    StatusAuthor authors[6] = {
        author("27821234567@s.whatsapp.net", "Alex", 1, 0, 90, 1),
        author("27820000001@s.whatsapp.net", "Mom", 3, 2, 12, 0),
        author("27820000003@s.whatsapp.net", "Pieter", 1, 1, 47, 0),
        author("27820000005@s.whatsapp.net", "Lindiwe", 2, 2, 150, 0),
        author("27820000002@s.whatsapp.net", "Sarah", 4, 0, 300, 0),
        author("27820000004@s.whatsapp.net", "Nadia", 1, 0, 600, 0),
    };
    StatusFeedDialogs feed;
    status_feed_dialogs_open(&feed);
    draw_app(LIST_HINTS);
    status_list_dialog_render(&feed.list, body(), authors, 6, NULL, NULL, 1);
    save("statuses");

    /* A text status on its own colour. */
    StatusUpdate text;
    status_update_init(&text);
    str_copy(text.id, sizeof(text.id), "T1");
    str_copy(text.author_jid, sizeof(text.author_jid), authors[1].jid);
    text.type = MESSAGE_TYPE_TEXT;
    text.text = "Sunday lunch at ours this week \xF0\x9F\x8D\x97\n\nEveryone welcome, bring a pudding!";
    text.background_argb = 0xFF128C7E;
    text.timestamp = (int64_t)time(NULL) - 12 * 60;
    StatusUpdate items[3];
    memset(items, 0, sizeof(items));
    items[0] = text;
    items[1] = text;
    items[2] = text;
    status_feed_dialogs_view(&feed, authors[1].jid, "Mom", 1, 3);
    draw_app(LIST_HINTS);
    status_viewer_dialog_render(&feed.viewer, body(), items, 3, s_thumbs, NULL, 1, NULL);
    save("status-text");

    /* r writes a reply to it, which goes to your chat with Mom. */
    status_viewer_dialog_key(&feed.viewer, 0, 'r');
    status_viewer_dialog_paste(&feed.viewer, "Count us in, I'll bring malva pudding");
    draw_app(LIST_HINTS);
    status_viewer_dialog_render(&feed.viewer, body(), items, 3, s_thumbs, NULL, 1, NULL);
    save("status-reply");
    status_viewer_dialog_key(&feed.viewer, 0, 27);

    /* Your own status: who saw it and who liked it. */
    StatusUpdate mine = text;
    mine.from_me = 1;
    mine.text = "Back from the mountains \xF0\x9F\xA5\xBE Thanks for all the messages!";
    mine.background_argb = 0xFF5696FF;
    status_feed_dialogs_view(&feed, authors[0].jid, "My status", 0, 1);
    draw_app(LIST_HINTS);
    status_viewer_dialog_render(&feed.viewer, body(), &mine, 1, s_thumbs, NULL, 1,
                                "Seen by 5 \xC2\xB7 \xE2\x9D\xA4\xEF\xB8\x8F 2");
    int64_t now = (int64_t)time(NULL);
    StatusViewer seen[5] = {
        { "27820000001@s.whatsapp.net", now - 4 * 60, "\xE2\x9D\xA4\xEF\xB8\x8F" },
        { "27820000003@s.whatsapp.net", now - 11 * 60, "" },
        { "27820000005@s.whatsapp.net", now - 25 * 60, "\xE2\x9D\xA4\xEF\xB8\x8F" },
        { "27820000002@s.whatsapp.net", now - 52 * 60, "" },
        { "27820000004@s.whatsapp.net", now - 80 * 60, "" },
    };
    status_feed_dialogs_show_viewers(&feed);
    status_viewers_dialog_render(&feed.viewers, body(), seen, 5, viewer_name, NULL, 1);
    save("status-viewers");
    status_feed_dialogs_close(&feed);
    status_feed_dialogs_open(&feed);

    /* A photo status with a caption. */
    StatusUpdate photo;
    status_update_init(&photo);
    str_copy(photo.id, sizeof(photo.id), "P1");
    photo.type = MESSAGE_TYPE_IMAGE;
    path_in(photo.media_path, sizeof(photo.media_path), s_pics, "landscape.png");
    photo.text = "Made it to the top \xF0\x9F\x8E\x89";
    photo.timestamp = (int64_t)time(NULL) - 47 * 60;
    status_feed_dialogs_view(&feed, authors[2].jid, "Pieter", 0, 1);
    draw_app(LIST_HINTS);
    status_viewer_dialog_render(&feed.viewer, body(), &photo, 1, s_thumbs, NULL, 1, NULL);
    save("status-photo");

    /* Writing a text status. */
    StatusComposerDialog c;
    status_composer_dialog_open(&c, 700);
    const char *words = "Braai at ours on Saturday from 2, all welcome";
    for (const char *p = words; *p; p++) status_composer_dialog_key(&c, 0, (unsigned char)*p);
    c.background = 2;
    draw_app(LIST_HINTS);
    status_composer_dialog_render(&c, body(), "Teal", 0xFF128C7E, 1);
    save("status-compose");

    /* On Baileys, + offers to switch backends. */
    ConfirmDialog confirm;
    confirm_dialog_open(&confirm, CONFIRM_USE_WHATSMEOW, "", "Posting statuses",
                        "Posting needs the whatsmeow backend. Switch now?",
                        "tawk will restart, and you will link this computer again with a QR code or pairing code. Your chats stay.",
                        "Switch to whatsmeow", 0);
    draw_app(LIST_HINTS);
    confirm_dialog_render(&confirm, body(), 0);
    save("status-switch");
}

/* The conversation and input of an open chat, where the camera works. */
static UiRect chat_area(void) { return (UiRect){ 1, SIDEBAR, ROWS - 4, COLS - SIDEBAR }; }

static void draw_chat(const char *footer) {
    draw_app(footer);
    static ComposerView composer;
    composer_view_init(&composer);
    composer_view_render(&composer, (UiRect){ ROWS - 3, SIDEBAR, 2, COLS - SIDEBAR }, 1, 1, -1, 1, "");
}

static void camera_scenes(void) {
    AttachMenu menu;
    attach_menu_open(&menu, 1);
    draw_chat(INPUT_HINTS);
    attach_menu_render(&menu, chat_area());
    save("attach-menu");

    char path[700];
    path_in(path, sizeof(path), s_pics, "webcam.png");
    RgbImage frame;
    memset(&frame, 0, sizeof(frame));
    if (rgb_image_load_file(path, &frame) != 0) { fprintf(stderr, "cannot load %s\n", path); return; }
    CameraView v;
    memset(&v, 0, sizeof(v));
    camera_view_open(&v);
    camera_view_set_frame(&v, &frame);
    draw_chat(INPUT_HINTS);
    camera_view_render(&v, chat_area(), 0);
    save("camera");

    camera_view_recording(&v);
    v.seconds = 5;
    draw_chat(INPUT_HINTS);
    camera_view_render(&v, chat_area(), 0);
    save("camera-video");

    camera_view_live(&v);
    camera_view_review(&v, "/tmp/photo.jpg");
    draw_chat(INPUT_HINTS);
    camera_view_render(&v, chat_area(), 0);
    save("camera-review");
    camera_view_close(&v);
    rgb_image_dispose(&frame);
}

/* ---- a conversation ------------------------------------------------------ */

/* Today at hour:minute local time (hours past 23 run into the next days). */
static int64_t local_today(int hour, int minute) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return (int64_t)mktime(&tm);
}

static void resolve(void *ctx, const char *jid, char *out, size_t size) {
    (void)ctx;
    str_copy(out, size, strstr(jid, "0001") ? "Lindiwe" : strstr(jid, "0002") ? "Jan" : strstr(jid, "0009") ? "Mom" : "You");
}

static void add_message(Message *list, int *n, const char *id, const char *sender, int from_me, int minutes_ago,
                        const char *text, const char *reactions) {
    Message *m = &list[(*n)++];
    message_init(m);
    str_copy(m->id, sizeof(m->id), id);
    str_copy(m->chat_jid, sizeof(m->chat_jid), "120363000000000001@g.us");
    str_copy(m->sender_jid, sizeof(m->sender_jid), sender);
    resolve(NULL, sender, m->sender_name, sizeof(m->sender_name));
    m->from_me = from_me;
    m->type = MESSAGE_TYPE_TEXT;
    m->status = from_me ? MESSAGE_STATUS_READ : MESSAGE_STATUS_DELIVERED;
    m->timestamp = (int64_t)time(NULL) - minutes_ago * 60;
    message_set_text(m, text);
    str_copy(m->reactions, sizeof(m->reactions), reactions ? reactions : "");
}

/* The app formats through the messaging manager; here the rules are applied directly. */
static int format_directly(void *ctx, const Message *m, StyledText *out) {
    (void)ctx;
    MentionList mentions;
    mention_list_parse(&mentions, m->mentions);
    MentionName names[MENTION_LIST_MAX];
    for (int k = 0; k < mentions.count; k++) {
        str_copy(names[k].user, sizeof(names[k].user), mentions.items[k].user);
        resolve(NULL, mentions.items[k].jid, names[k].name, sizeof(names[k].name));
    }
    return whatsapp_markup_parse(m->text, names, mentions.count, out);
}

static void preview_directly(void *ctx, const Message *m, char *out, size_t size) {
    (void)ctx;
    whatsapp_markup_plain(m->text, NULL, 0, out, size);
}

/* The statuses the replies in the status scene answer. */
static int find_status(void *ctx, const char *id, StatusUpdate *out) {
    (void)ctx;
    status_update_init(out);
    str_copy(out->id, sizeof(out->id), id);
    if (strcmp(id, "MINE") == 0) {
        out->type = MESSAGE_TYPE_TEXT;
        out->text = strdup("Back from the mountains \xF0\x9F\xA5\xBE Thanks for all the messages!");
        out->background_argb = 0xFF5696FF;
        out->from_me = 1;
        return 0;
    }
    if (strcmp(id, "GARDEN") == 0) {
        out->type = MESSAGE_TYPE_IMAGE;
        out->text = strdup("The roses are out \xF0\x9F\x8C\xB9");
        path_in(out->media_path, sizeof(out->media_path), s_pics, "landscape.png");
        return 0;
    }
    return -1;
}

/* Replies to statuses show the status they answer. */
static void status_reply_scene(void) {
    Message msgs[4];
    int n = 0;
    const char *mom = "27820000009@s.whatsapp.net";
    add_message(msgs, &n, "R1", "27821234567@s.whatsapp.net", 1, 180, "Beautiful! Is that the new bed by the gate?", NULL);
    str_copy(msgs[n - 1].quoted_id, sizeof(msgs[n - 1].quoted_id), "GARDEN");
    str_copy(msgs[n - 1].quoted_sender, sizeof(msgs[n - 1].quoted_sender), mom);
    msgs[n - 1].quoted_status = 1;
    add_message(msgs, &n, "R2", mom, 0, 170, "Yes, planted in March \xF0\x9F\x98\x8A", NULL);
    add_message(msgs, &n, "R3", mom, 0, 6, "Welcome back! Did you get to the top this time?", NULL);
    str_copy(msgs[n - 1].quoted_id, sizeof(msgs[n - 1].quoted_id), "MINE");
    str_copy(msgs[n - 1].quoted_sender, sizeof(msgs[n - 1].quoted_sender), "27821234567@s.whatsapp.net");
    msgs[n - 1].quoted_status = 1;
    for (int i = 0; i < n; i++) str_copy(msgs[i].chat_jid, sizeof(msgs[i].chat_jid), mom);
    NameResolver names = { NULL, resolve };
    StatusSource statuses = { NULL, find_status };
    MessageViewContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.title = "Mom";
    ctx.status = "";
    ctx.use_24h = 1;
    ctx.playing_path = "";
    ctx.names = &names;
    ctx.thumbs = s_thumbs;
    ctx.subtitle = "Hey there! I am using WhatsApp.";
    ctx.activity = "";
    ctx.jid = mom;
    ctx.statuses = &statuses;
    static MessageView view;
    message_view_init(&view);
    draw_chat(INPUT_HINTS);
    message_view_render(&view, chat_area(), msgs, n, &ctx);
    save("status-reply-chat");
    message_view_dispose(&view);
    for (int i = 0; i < n; i++) message_dispose(&msgs[i]);
}

static void conversation_scene(void) {
    Message msgs[16];
    int n = 0;
    add_message(msgs, &n, "C1", "27820000001@s.whatsapp.net", 0, 42, "Staging is up, can someone check the *login page*?", "\xF0\x9F\x91\x8D 2");
    add_message(msgs, &n, "C2", "27821234567@s.whatsapp.net", 1, 40, "On it, looks good from my side _so far_", "\xE2\x9D\xA4\xEF\xB8\x8F");
    add_message(msgs, &n, "C3", "27820000002@s.whatsapp.net", 0, 31, "Same here @27820000001. Shipping it this afternoon:\n- run `make release`\n- ~tag it by hand~ the script tags it", NULL);
    message_set_mentions(&msgs[n - 1], "27820000001@s.whatsapp.net\t27820000001\n");
    add_message(msgs, &n, "C4", "27820000002@s.whatsapp.net", 0, 29, "Thanks everyone for the quick turnaround", "\xF0\x9F\x8E\x89 3");
    add_message(msgs, &n, "C5", "27820000002@s.whatsapp.net", 0, 12, "Retro is on Thursday at 10", NULL);
    msgs[n - 1].forwarded = 1;
    add_message(msgs, &n, "C6", "27820000001@s.whatsapp.net", 0, 2, "Release notes are up https://example.com/tawk/0.7", NULL);
    message_set_link(&msgs[n - 1], link_preview_create("https://example.com/tawk/0.7", "tawk 0.7 release notes",
                                                       "Formatting, mentions, forwarding, scheduled messages and link previews."));
    char card[700];
    path_in(card, sizeof(card), s_pics, "linkcard.jpg");
    FILE *jpg = fopen(card, "rb");
    if (jpg) {
        static unsigned char bytes[65536];
        size_t len = fread(bytes, 1, sizeof(bytes), jpg);
        fclose(jpg);
        message_set_thumbnail(&msgs[n - 1], bytes, (int)len);
    }
    NameResolver names = { NULL, resolve };
    MessageViewContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.title = "Dev team";
    ctx.status = "";
    ctx.is_group = 1;
    ctx.use_24h = 1;
    ctx.playing_path = "";
    ctx.names = &names;
    ctx.thumbs = s_thumbs;
    ctx.subtitle = "5 members";
    ctx.activity = "";
    ctx.jid = "120363000000000001@g.us";
    static const MessageFormatter formatter = { NULL, format_directly, preview_directly };
    ctx.formatter = &formatter;
    static MessageView view;
    message_view_init(&view);
    draw_chat(INPUT_HINTS);
    message_view_render(&view, chat_area(), msgs, n, &ctx);
    save("conversation");

    /* Scrolled back in a long chat: the badge that returns to the newest message. */
    static const char *const OLDER[] = {
        "Morning all, standup in ten", "I will be five minutes late", "No rush, we start with the board",
        "The importer finished overnight", "Any rows rejected?", "Twelve, all with a missing date",
        "I will send the list to finance", "Thanks. Can we close the ticket then?", "Yes, closing it now",
        "Lunch at the usual place?", "Count me in", "Me too, 12:30?", "Booked a table for six",
    };
    Message older[16];
    int older_n = 0;
    for (int i = 0; i < (int)(sizeof(OLDER) / sizeof(OLDER[0])); i++) {
        char id[8];
        snprintf(id, sizeof(id), "O%d", i);
        const char *who = i % 3 == 1 ? "27821234567@s.whatsapp.net" : i % 3 == 0 ? "27820000001@s.whatsapp.net" : "27820000002@s.whatsapp.net";
        add_message(older, &older_n, id, who, i % 3 == 1, 300 - i * 9, OLDER[i], NULL);
    }
    static MessageView back;
    message_view_init(&back);
    back.scroll = 6;
    back.has_newer = 1;
    draw_chat(INPUT_HINTS);
    message_view_render(&back, chat_area(), older, older_n, &ctx);
    save("conversation-older");
    for (int i = 0; i < older_n; i++) message_dispose(&older[i]);

    /* Typing "@Li" in the same group lists the members that fit. */
    draw_app(INPUT_HINTS);
    message_view_render(&view, chat_area(), msgs, n, &ctx);
    static ComposerView typing;
    composer_view_init(&typing);
    composer_view_set_text(&typing, "Great work @Li");
    UiRect input = { ROWS - 3, SIDEBAR, 2, COLS - SIDEBAR };
    composer_view_render(&typing, input, 1, 1, -1, 1, "");
    MentionCandidate fit[2] = { { "27820000001@s.whatsapp.net", "Lindiwe" }, { "27820000007@s.whatsapp.net", "Lize" } };
    MentionSuggestions list;
    memset(&list, 0, sizeof(list));
    mention_suggestions_open(&list, fit, 2, 11, 14);
    mention_suggestions_render(&list, input);
    save("mentions");
    /* The same chat with a message waiting to be sent this evening. */
    ScheduledMessage later;
    scheduled_message_init(&later);
    str_copy(later.id, sizeof(later.id), "S1");
    later.text = "Reminder: release retro tomorrow at 10";
    later.due_at = local_today(18, 0);
    ctx.scheduled = &later;
    ctx.scheduled_count = 1;
    message_view_init(&view);
    draw_chat(INPUT_HINTS);
    message_view_render(&view, chat_area(), msgs, n, &ctx);
    save("scheduled-bubble");
    for (int i = 0; i < n; i++) message_dispose(&msgs[i]);
}

static void chat_title(void *ctx, const char *jid, char *out, size_t size) {
    (void)ctx;
    for (int i = 0; i < s_chat_count; i++) if (strcmp(s_chats[i].jid, jid) == 0) { str_copy(out, size, s_chats[i].name); return; }
    str_copy(out, size, jid);
}

static void scheduled_scene(void) {
    ScheduledMessage items[3];
    const char *jids[3] = { "120363000000000001@g.us", "27820000001@s.whatsapp.net", "120363000000000002@g.us" };
    const char *texts[3] = { "Reminder: release retro tomorrow at 10", "Happy birthday Mom! Lunch on Sunday?",
                             "Next month's book is The Overstory" };
    int64_t dues[3] = { local_today(18, 0), local_today(24 + 8, 0), local_today(24 * 3 + 19, 30) };
    for (int i = 0; i < 3; i++) {
        scheduled_message_init(&items[i]);
        snprintf(items[i].id, sizeof(items[i].id), "S%d", i);
        str_copy(items[i].chat_jid, sizeof(items[i].chat_jid), jids[i]);
        items[i].text = (char *)texts[i];
        items[i].due_at = dues[i];
    }
    ScheduledListDialog d;
    scheduled_list_dialog_open(&d);
    draw_app(LIST_HINTS);
    scheduled_list_dialog_render(&d, body(), items, 3, chat_title, NULL, 1);
    save("scheduled");
}

/* ---- several accounts ------------------------------------------------------ */

static Account an_account(AccountId id, const char *label, const char *jid, int colour, int primary, AccountAgentAccess access) {
    Account a;
    memset(&a, 0, sizeof(a));
    a.id = id;
    str_copy(a.label, sizeof(a.label), label);
    str_copy(a.jid, sizeof(a.jid), jid);
    str_copy(a.name, sizeof(a.name), "Alex");
    a.colour = colour;
    a.is_primary = primary;
    a.agent_access = access;
    return a;
}

/* Two numbers in one tawk: the chat list with a badge on each row, the list of
 * accounts, and one person's two chats shown as one conversation. */
static void accounts_scenes(void) {
    Account main_account = an_account(1, "main", "27821234567@s.whatsapp.net", 0, 1, ACCOUNT_AGENT_FOLLOW);
    Account work = an_account(2, "work", "27825550100@s.whatsapp.net", 1, 0, ACCOUNT_AGENT_READ);
    Account side = an_account(3, "side", "", 2, 0, ACCOUNT_AGENT_OFF);
    side.name[0] = '\0';                                  /* not linked yet: no name is known */
    account_badge_make(&s_badges[0], &main_account);
    account_badge_make(&s_badges[1], &work);
    s_badge_count = 2;
    s_account_chip = "All";
    /* Mom writes to both numbers and is one row; the team and Nadia are on the work number. */
    for (int i = 0; i < s_chat_count; i++) {
        Chat *c = &s_chats[i];
        int at_work = !strcmp(c->name, "Dev team") || !strcmp(c->name, "Nadia");
        c->account = at_work ? work.id : main_account.id;
        c->accounts = !strcmp(c->name, "Mom") ? 3u : at_work ? 2u : 1u;
    }
    draw_app("type to search \xC2\xB7 Enter open \xC2\xB7 Alt+O options \xC2\xB7 F2");
    save("accounts-chats");

    static AccountsDialog dialog;
    accounts_dialog_open(&dialog);
    AccountsDialogRow rows[3] = {
        { main_account, AUTH_STATE_CONNECTED, 1, 1 },
        { work, AUTH_STATE_CONNECTED, 0, 1 },
        { side, AUTH_STATE_NEEDS_LOGIN, 0, 0 },
    };
    accounts_dialog_render(&dialog, body(), rows, 3);
    save("accounts-list");

    /* The same person on both numbers, as one conversation. */
    Message msgs[8];
    AccountId owners[8];
    int n = 0;
    const char *mom = "27820000001@s.whatsapp.net", *me = "27821234567@s.whatsapp.net";
    add_message(msgs, &n, "A1", mom, 0, 190, "Are you coming on Sunday?", NULL);                 owners[n - 1] = main_account.id;
    add_message(msgs, &n, "A2", me, 1, 185, "Yes, we will be there by 12", NULL);                owners[n - 1] = main_account.id;
    add_message(msgs, &n, "A3", mom, 0, 44, "Is this your work number? The invoice is here", NULL); owners[n - 1] = work.id;
    add_message(msgs, &n, "A4", me, 1, 41, "It is, thanks. I will send it on tonight", NULL);    owners[n - 1] = work.id;
    add_message(msgs, &n, "A5", mom, 0, 6, "Bring the big pot please \xF0\x9F\x8D\xB2", NULL); owners[n - 1] = main_account.id;
    for (int i = 0; i < n; i++) str_copy(msgs[i].chat_jid, sizeof(msgs[i].chat_jid), mom);
    NameResolver names = { NULL, resolve };
    MessageViewContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.title = "Mom";
    ctx.status = "";
    ctx.use_24h = 1;
    ctx.playing_path = "";
    ctx.names = &names;
    ctx.thumbs = s_thumbs;
    ctx.subtitle = "on main and work";
    ctx.activity = "";
    ctx.jid = mom;
    static const MessageFormatter formatter = { NULL, format_directly, preview_directly };
    ctx.formatter = &formatter;
    ctx.owners = owners;
    ctx.badges = s_badges;
    ctx.badge_count = s_badge_count;
    static MessageView view;
    message_view_init(&view);
    draw_app(INPUT_HINTS);
    message_view_render(&view, chat_area(), msgs, n, &ctx);
    static ComposerView composer;
    composer_view_init(&composer);
    str_copy(composer.sending_as, sizeof(composer.sending_as), "as main (Alt+A changes)");
    composer_view_render(&composer, (UiRect){ ROWS - 3, SIDEBAR, 2, COLS - SIDEBAR }, 1, 1, -1, 1, "");
    save("accounts-merged");
    for (int i = 0; i < n; i++) message_dispose(&msgs[i]);

    /* The scenes after this one show a tawk with one account again. */
    s_badge_count = 0;
    s_account_chip = "";
    for (int i = 0; i < s_chat_count; i++) { s_chats[i].account = ACCOUNT_ID_NONE; s_chats[i].accounts = 0; }
}

/* ---- the Agents tab ------------------------------------------------------- */

#define AGENTS_NOW_MS 3600000LL                         /* the panel's clock: any monotonic time */

static void agents_chat_name(void *ctx, AccountId account, const char *jid, char *out, size_t size) {
    (void)account;
    if (strcmp(jid, "120363000000000003@g.us") == 0) { str_copy(out, size, "Old group"); return; }
    chat_title(ctx, jid, out, size);
}

static void ask_agent(ApprovalQueue *q, int id, const char *op, const char *jid, const char *chat, const char *action,
                      const char *text, ApprovalRisk risk, int seconds_ago, int window_s) {
    ApprovalRequest r;
    memset(&r, 0, sizeof(r));
    r.id = id;
    r.origin = CONTROL_ORIGIN_MCP;
    str_copy(r.client, sizeof(r.client), "tawk-mcp");
    str_copy(r.op, sizeof(r.op), op);
    str_copy(r.chat_jid, sizeof(r.chat_jid), jid);
    str_copy(r.chat_name, sizeof(r.chat_name), chat);
    str_copy(r.action, sizeof(r.action), action);
    r.text = (char *)text;
    r.editable = text != NULL;
    r.danger = risk == APPROVAL_RISK_HIGH;
    r.risk = risk;
    r.asked_ms = AGENTS_NOW_MS - seconds_ago * 1000LL;
    r.expires_ms = r.asked_ms + window_s * 1000LL;
    approval_queue_prompt(q)->ask(approval_queue_prompt(q), &r);
}

static AutomationEntry log_entry(int minutes_ago, ControlOrigin origin, const char *client, const char *op, const char *jid,
                                 const char *summary, AutomationOutcome outcome) {
    AutomationEntry e;
    memset(&e, 0, sizeof(e));
    e.at = (int64_t)time(NULL) - minutes_ago * 60;
    e.origin = origin;
    str_copy(e.client, sizeof(e.client), client);
    str_copy(e.op, sizeof(e.op), op);
    str_copy(e.chat_jid, sizeof(e.chat_jid), jid);
    str_copy(e.summary, sizeof(e.summary), summary);
    e.outcome = outcome;
    return e;
}

static void agents_panel_draw(AgentsPanel *p, const AgentsPanelModel *m, const char *name) {
    draw_app(LIST_HINTS);
    agents_panel_render(p, body(), m);
    save(name);
}

static void agents_scenes(void) {
    const char *mom = "27820000001@s.whatsapp.net", *book = "120363000000000002@g.us", *old = "120363000000000003@g.us";
    const char *sarah = "27820000002@s.whatsapp.net", *dev = "120363000000000001@g.us";
    const char *pieter = "27820000003@s.whatsapp.net", *nadia = "27820000004@s.whatsapp.net";
    ApprovalQueue *q = approval_queue_create();
    ask_agent(q, 41, "send_message", mom, "Mom", "send a message", "Running 10 minutes late, sorry! Start without me.",
              APPROVAL_RISK_MEDIUM, 125, 300);
    ask_agent(q, 42, "react", book, "Book club", "react with \xF0\x9F\x91\x8D", NULL, APPROVAL_RISK_LOW, 48, 300);
    ask_agent(q, 43, "delete_chat", old, "Old group", "delete the whole chat, here and on your phone", NULL,
              APPROVAL_RISK_HIGH, 31, 120);

    Settings settings;
    settings_set_defaults(&settings);
    settings.use_24h_clock = 1;
    settings.control_socket = 1;
    str_copy(settings.automation_access, sizeof(settings.automation_access), "send");

    AutomationStatus status;
    memset(&status, 0, sizeof(status));
    status.listening = 1;
    status.mcp_sessions = 1;
    status.cli_sessions = 1;
    status.waiting = approval_queue_count(q);
    str_copy(status.socket_path, sizeof(status.socket_path), "/run/user/1000/tawk/control.sock");
    status.sessions[0] = (AutomationSession){ 3, "tawk-mcp", CONTROL_ORIGIN_MCP, (int64_t)time(NULL) - 95 * 60, 38, 2, 0 };
    status.sessions[1] = (AutomationSession){ 5, "tawk", CONTROL_ORIGIN_CLI, (int64_t)time(NULL) - 3 * 60, 1, 0, 0 };
    status.session_count = 2;

    AutomationEntry log[] = {
        log_entry(2, CONTROL_ORIGIN_MCP, "tawk-mcp", "send_message", sarah, "Sounds good, see you at 7 at the usual place", AUTOMATION_OUTCOME_APPROVED),
        log_entry(3, CONTROL_ORIGIN_MCP, "tawk-mcp", "send_message", mom, "Leaving now, home by six", AUTOMATION_OUTCOME_SELF_APPROVED),
        log_entry(4, CONTROL_ORIGIN_MCP, "tawk-mcp", "react", book, "\xF0\x9F\x91\x8D", AUTOMATION_OUTCOME_ALLOWED),
        log_entry(7, CONTROL_ORIGIN_MCP, "tawk-mcp", "react", dev, "\xF0\x9F\x8E\x89", AUTOMATION_OUTCOME_ALLOWED),
        log_entry(9, CONTROL_ORIGIN_MCP, "tawk-mcp", "read_messages", dev, "last 40 messages", AUTOMATION_OUTCOME_READ),
        log_entry(11, CONTROL_ORIGIN_MCP, "tawk-mcp", "send_message", dev, "Staging looks good from here, shipping after lunch", AUTOMATION_OUTCOME_APPROVED),
        log_entry(18, CONTROL_ORIGIN_MCP, "tawk-mcp", "delete_message", nadia, "delete \"Thanks for the book tip\" for everyone", AUTOMATION_OUTCOME_DECLINED),
        log_entry(26, CONTROL_ORIGIN_CLI, "tawk", "send_message", mom, "Home by six, I'll bring bread", AUTOMATION_OUTCOME_DONE),
        log_entry(34, CONTROL_ORIGIN_MCP, "tawk-mcp", "schedule_message", pieter, "Happy birthday Pieter! Drinks on Friday?", AUTOMATION_OUTCOME_TIMED_OUT),
        log_entry(47, CONTROL_ORIGIN_MCP, "tawk-mcp", "set_profile", "", "about: Out hiking, back on Monday", AUTOMATION_OUTCOME_REFUSED),
        log_entry(52, CONTROL_ORIGIN_MCP, "tawk-mcp", "mark_read", book, "12 messages", AUTOMATION_OUTCOME_ALLOWED),
        log_entry(63, CONTROL_ORIGIN_MCP, "tawk-mcp", "send_message", sarah, "Are we still on for Saturday?", AUTOMATION_OUTCOME_DECLINED),
        log_entry(78, CONTROL_ORIGIN_MCP, "tawk-mcp", "draft_message", mom, "Happy birthday Mom! Lunch on Sunday?", AUTOMATION_OUTCOME_DONE),
        log_entry(88, CONTROL_ORIGIN_MCP, "tawk-mcp", "send_message", sarah, "Can you send me the address again?", AUTOMATION_OUTCOME_RATE_LIMITED),
        log_entry(95, CONTROL_ORIGIN_MCP, "tawk-mcp", "hello", "", "tawk-mcp 0.3 acting for a model", AUTOMATION_OUTCOME_CONNECTED),
    };
    AgentsPanelModel m = { q, &status, log, (int)(sizeof(log) / sizeof(log[0])), &settings, AGENTS_NOW_MS, agents_chat_name, NULL, NULL, NULL };

    s_agents_waiting = approval_queue_count(q);
    s_agents_high = approval_queue_high_count(q);
    s_agents_connected = status.session_count;

    s_agents_tab = 1;
    static AgentsPanel p;
    agents_panel_open(&p, AGENTS_VIEW_QUEUE);
    agents_panel_draw(&p, &m, "agents-queue");

    /* e: the text, ready to change before it goes. */
    p.editing = 1;
    text_field_set(&p.edit, "Running 15 minutes late, sorry! Start without me, I'll catch up.");
    agents_panel_draw(&p, &m, "agents-edit");
    p.editing = 0;

    /* Shift+A on the HIGH request asks once more, starting on Keep. */
    p.selected[AGENTS_VIEW_QUEUE] = 2;
    p.confirming = 43;
    agents_panel_draw(&p, &m, "agents-high");
    p.confirming = 0;
    p.selected[AGENTS_VIEW_QUEUE] = 0;

    p.view = AGENTS_VIEW_LOG;
    agents_panel_draw(&p, &m, "agents-log");

    p.view = AGENTS_VIEW_PERMISSIONS;
    p.selected[AGENTS_VIEW_PERMISSIONS] = 1;
    agents_panel_draw(&p, &m, "agents-permissions");

    /* Access admin: an agent with the admin token answers its own sends in the chats named. */
    str_copy(settings.automation_access, sizeof(settings.automation_access), "admin");
    agents_panel_draw(&p, &m, "agents-admin");
    str_copy(settings.automation_access, sizeof(settings.automation_access), "send");

    /* The main screen: the Agentic tab counts what waits. */
    s_agents_tab = 0;
    draw_app(LIST_HINTS);
    save("agentic-tab");

    s_agents_waiting = s_agents_high = s_agents_connected = s_agents_tab = 0;
    approval_queue_destroy(q);
}

static void forward_scene(void) {
    ChatPicker picker;
    chat_picker_open(&picker, "Forward to");
    chat_picker_render(&picker, body(), s_chats, s_chat_count);
    chat_picker_key(&picker, 0, ' ');                   /* tick the first two chats */
    chat_picker_key(&picker, 1, KEY_DOWN);
    chat_picker_key(&picker, 0, ' ');
    chat_picker_key(&picker, 1, KEY_DOWN);
    draw_chat(INPUT_HINTS);
    chat_picker_render(&picker, body(), s_chats, s_chat_count);
    save("forward");
}

/* Settings, Automation: the chats an admin agent may answer in by itself, each with a switch. */
static void self_chats_scene(void) {
    ChatToggleDialog dialog;
    chat_toggle_dialog_open(&dialog, "Chats an agent may answer in by itself", "All chats agents may use");
    chat_toggle_dialog_render(&dialog, body(), s_chats, s_chat_count);
    chat_toggle_dialog_key(&dialog, 1, KEY_DOWN);
    chat_toggle_dialog_key(&dialog, 0, ' ');             /* switch the first and the third on */
    chat_toggle_dialog_render(&dialog, body(), s_chats, s_chat_count);
    chat_toggle_dialog_key(&dialog, 1, KEY_DOWN);
    chat_toggle_dialog_key(&dialog, 1, KEY_DOWN);
    chat_toggle_dialog_key(&dialog, 0, ' ');
    draw_app(LIST_HINTS);
    chat_toggle_dialog_render(&dialog, body(), s_chats, s_chat_count);
    save("self-approval-chats");
}

/* ---- the settings panel, on made-up settings ------------------------------ */

static Settings s_shown_settings;
static const Settings *shown_settings(void *ctx) { (void)ctx; return &s_shown_settings; }
static int shown_apply(void *ctx, const Settings *updated) { (void)ctx; s_shown_settings = *updated; return 0; }
static IThemeRepository *shown_themes(void *ctx) { return ctx; }
static void shown_preview(void *ctx, const Theme *theme) { (void)ctx; (void)theme; }
static void shown_action(void *ctx, MenuAction action) { (void)ctx; (void)action; }
static void shown_info(void *ctx, MenuInfo info, char *out, size_t size) {
    (void)ctx;
    str_copy(out, size, info == MENU_INFO_AGENTS ? "listening \xC2\xB7 1 agent connected" : info == MENU_INFO_SELF_CHATS ? "2 chats" : "");
}

/* Settings, then each entry of `path` in turn: that many steps down, and opened. */
static void settings_scene(IThemeRepository *themes, const int *path, int depth, const char *name) {
    settings_set_defaults(&s_shown_settings);
    s_shown_settings.control_socket = 1;
    s_shown_settings.automation_push_read = 1;
    s_shown_settings.automation_push_reactions = 1;
    str_copy(s_shown_settings.automation_access, sizeof(s_shown_settings.automation_access), "admin");
    SettingsPanelHost host = { themes, shown_settings, shown_apply, shown_themes, shown_preview, shown_action, shown_info };
    static SettingsPanel panel;
    settings_panel_init(&panel, host);
    settings_panel_open(&panel);
    for (int d = 0; d < depth; d++) {
        draw_app(LIST_HINTS);
        settings_panel_render(&panel, body(), 0);
        for (int i = 0; i < path[d]; i++) settings_panel_key(&panel, 1, KEY_DOWN, 0);
        settings_panel_key(&panel, 0, '\n', 0);
    }
    draw_app(LIST_HINTS);
    settings_panel_render(&panel, body(), 0);
    save(name);
}

/* Something typed, the clear button beside it, and the question it asks. */
static void clear_input_scene(void) {
    draw_app(INPUT_HINTS);
    static ComposerView composer;
    composer_view_init(&composer);
    composer_view_set_text(&composer, "Running late, start without me and I will catch up");
    composer_view_render(&composer, (UiRect){ ROWS - 3, SIDEBAR, 2, COLS - SIDEBAR }, 1, 1, -1, 1, "");
    save("clear-input-button");
    ConfirmDialog confirm;
    confirm_dialog_open(&confirm, CONFIRM_CLEAR_INPUT, "", " Clear message ", "Clear what you typed?",
                        "The text in the message box will be removed. It has not been sent.", "Clear", 0);
    confirm_dialog_render(&confirm, body(), 0);
    save("clear-input");
}

static void splash_scene(void) {
    SplashView v;
    splash_view_start(&v, 0);
    splash_view_render(&v, (UiRect){ 0, 0, ROWS, COLS }, 1800, "v" APP_VERSION, "Connecting to WhatsApp\xE2\x80\xA6");
    save("splash");
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: scenes OUT_DIR PICTURE_DIR\n"); return 2; }
    str_copy(s_out, sizeof(s_out), argv[1]);
    str_copy(s_pics, sizeof(s_pics), argv[2]);
    setlocale(LC_ALL, "C.UTF-8");
    FILE *out = fopen("/dev/null", "w"), *in = fopen("/dev/null", "r");
    SCREEN *screen = out && in ? newterm("xterm-256color", out, in) : NULL;
    if (!screen) { fprintf(stderr, "no curses screen\n"); return 1; }
    resizeterm(ROWS, COLS);
    tui_palette_init();
    IThemeRepository *themes = json_theme_repository_create("themes", NULL);
    const char *theme = getenv("TAWK_SHOT_THEME");         /* another theme, to check colours */
    int index = themes->index_of(themes, theme && *theme ? theme : "whatsapp-dark");
    tui_palette_apply(themes->at(themes, index < 0 ? 0 : index));
    s_thumbs = thumbnail_cache_create(16);
    make_chats();

    profile_scenes();
    status_scenes();
    camera_scenes();
    conversation_scene();
    status_reply_scene();
    forward_scene();
    self_chats_scene();
    clear_input_scene();
    settings_scene(themes, (const int[]){ 7 }, 1, "settings-automation");
    settings_scene(themes, (const int[]){ 7, 8 }, 2, "settings-agent-events");
    settings_scene(themes, (const int[]){ 7, 9 }, 2, "settings-self-approval");
    scheduled_scene();
    agents_scenes();
    accounts_scenes();
    splash_scene();

    thumbnail_cache_destroy(s_thumbs);
    themes->destroy(themes);
    endwin();
    delscreen(screen);
    return 0;
}
