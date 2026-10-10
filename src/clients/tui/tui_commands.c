#include "tui_app_state.h"

#include "utilities/clock_util.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Every /command, its argument hint and help. Handlers only call the app's
 * public operations, so commands, keys and menus share one behaviour. */

static const char *open_chat_or_warn(TuiApp *app) {
    const char *jid = messaging_manager_open_jid(app->deps.messaging);
    if (!jid[0]) { tui_app_toast(app, "Open a chat first", 1); return NULL; }
    return jid;
}

static int last_message(TuiApp *app, int incoming_only) {
    int count = 0;
    const Message *msgs = tui_app_messages(app, &count);
    for (int i = count - 1; i >= 0; i--) if (!incoming_only || !msgs[i].from_me) return i;
    return -1;
}

static void cmd_help(TuiApp *app, const char *args)      { (void)args; tui_app_open_help(app); }
static void cmd_search(TuiApp *app, const char *args)    { tui_app_open_search(app, args); }
static void cmd_settings(TuiApp *app, const char *args)  { (void)args; settings_panel_open(&app->settings_panel); }
static void cmd_quit(TuiApp *app, const char *args)      { (void)args; tui_app_quit(app); }
static void cmd_dnd(TuiApp *app, const char *args)       { (void)args; tui_app_toggle_dnd(app); }
static void cmd_voice(TuiApp *app, const char *args)     { (void)args; tui_app_toggle_recording(app); }
static void cmd_lock(TuiApp *app, const char *args)      { (void)args; tui_app_start_screensaver(app); }
static void cmd_profile(TuiApp *app, const char *args)   { (void)args; tui_app_open_profile(app); }
static void cmd_status(TuiApp *app, const char *args)    { (void)args; tui_app_open_status(app); }
static void cmd_statuses(TuiApp *app, const char *args)  { (void)args; tui_app_open_statuses(app); }
static void cmd_later(TuiApp *app, const char *args)     { tui_app_schedule_later(app, args); }
static void cmd_scheduled(TuiApp *app, const char *args) { (void)args; tui_app_open_scheduled(app); }
static void cmd_agents(TuiApp *app, const char *args)    { (void)args; tui_app_open_agents(app); }

static void cmd_mute(TuiApp *app, const char *args) {
    const char *jid = open_chat_or_warn(app);
    if (!jid) return;
    int64_t until = -1;
    const char *label = "until you unmute it";
    if (!strcmp(args, "8h")) { until = (int64_t)time(NULL) + 8 * 3600; label = "for 8 hours"; }
    else if (!strcmp(args, "1w")) { until = (int64_t)time(NULL) + 7 * 86400; label = "for 1 week"; }
    else if (args[0] && strcmp(args, "always")) { tui_app_toast(app, "Use /mute 8h, /mute 1w or /mute always", 1); return; }
    messaging_manager_mute_until(app->deps.messaging, jid, until);
    char msg[96];
    snprintf(msg, sizeof(msg), "\xF0\x9F\x94\x95 Muted %s", label);
    tui_app_toast(app, msg, 0);
}

static void cmd_unmute(TuiApp *app, const char *args) {
    (void)args;
    const char *jid = open_chat_or_warn(app);
    if (jid) { messaging_manager_mute_until(app->deps.messaging, jid, 0); tui_app_toast(app, "\xF0\x9F\x94\x94 Unmuted", 0); }
}

static void cmd_pin(TuiApp *app, const char *args) {
    (void)args;
    const char *jid = open_chat_or_warn(app);
    const Chat *c = messaging_manager_open_chat_info(app->deps.messaging);
    if (jid && c && !c->is_pinned) messaging_manager_toggle_pin(app->deps.messaging, jid);
}

static void cmd_unpin(TuiApp *app, const char *args) {
    (void)args;
    const char *jid = open_chat_or_warn(app);
    const Chat *c = messaging_manager_open_chat_info(app->deps.messaging);
    if (jid && c && c->is_pinned) messaging_manager_toggle_pin(app->deps.messaging, jid);
}

static void cmd_archive(TuiApp *app, const char *args) {
    (void)args;
    const char *jid = open_chat_or_warn(app);
    if (jid) { messaging_manager_set_archived(app->deps.messaging, jid, 1); tui_app_toast(app, "\xF0\x9F\x97\x84 Archived", 0); }
}

static void cmd_unarchive(TuiApp *app, const char *args) {
    (void)args;
    const char *jid = open_chat_or_warn(app);
    if (jid) { messaging_manager_set_archived(app->deps.messaging, jid, 0); tui_app_toast(app, "Moved back to chats", 0); }
}

static void cmd_tone(TuiApp *app, const char *args) {
    const char *jid = open_chat_or_warn(app);
    if (!jid) return;
    if (!args[0]) { str_copy(app->tone_jid, sizeof(app->tone_jid), jid); tui_app_open_file_picker(app, FILE_PICKER_FOR_TONE); return; }
    if (!strcmp(args, "default")) { messaging_manager_set_tone(app->deps.messaging, jid, ""); tui_app_toast(app, "Default tone", 0); return; }
    if (!strcmp(args, "none")) { messaging_manager_set_tone(app->deps.messaging, jid, "none"); tui_app_toast(app, "No sound for this chat", 0); return; }
    char path[1024];
    path_expand_home(args, path, sizeof(path));
    if (!path_is_regular_file(path)) { tui_app_toast(app, "That sound file does not exist", 1); return; }
    messaging_manager_set_tone(app->deps.messaging, jid, path);
    tui_app_toast(app, "\xF0\x9F\x8E\xB5 Tone set for this chat", 0);
}

static void cmd_theme(TuiApp *app, const char *args) {
    const char *jid = open_chat_or_warn(app);
    if (!jid) return;
    if (!args[0]) { tui_app_open_theme_picker(app, jid); return; }
    if (!strcmp(args, "default")) { messaging_manager_set_chat_theme(app->deps.messaging, jid, ""); tui_app_invalidate(app); return; }
    IThemeRepository *themes = settings_manager_themes(app->deps.settings);
    if (themes->index_of(themes, args) < 0) { tui_app_toast(app, "Unknown theme id; run /theme to browse", 1); return; }
    messaging_manager_set_chat_theme(app->deps.messaging, jid, args);
    app->conversation_theme_valid = 0;
    tui_app_toast(app, "\xF0\x9F\x8E\xA8 Chat theme applied", 0);
}

static void cmd_info(TuiApp *app, const char *args) {
    (void)args;
    char jid[128];
    str_copy(jid, sizeof(jid), messaging_manager_open_jid(app->deps.messaging));
    tui_app_open_contact(app, jid);
}

static void cmd_softlock(TuiApp *app, const char *args) {
    (void)args;
    tui_app_toggle_soft_lock(app, messaging_manager_open_jid(app->deps.messaging));
}

static void cmd_transcripts(TuiApp *app, const char *args) {
    (void)args;
    tui_app_step_show_transcripts(app, messaging_manager_open_jid(app->deps.messaging));
}

static void cmd_paste(TuiApp *app, const char *args) {
    (void)args;
    tui_app_paste_image(app);
}

static void cmd_camera(TuiApp *app, const char *args) {
    (void)args;
    tui_app_open_camera(app);
}

static void cmd_attach(TuiApp *app, const char *args) {
    if (!open_chat_or_warn(app)) return;
    if (!args[0]) { tui_app_open_file_picker(app, FILE_PICKER_FOR_ATTACHMENT); return; }
    char path[1024];
    path_expand_home(args, path, sizeof(path));
    tui_app_attach(app, path);
}

static void cmd_react(TuiApp *app, const char *args) {
    int i = last_message(app, 1);
    if (i < 0) { tui_app_toast(app, "No message to react to", 1); return; }
    if (!args[0]) { tui_app_open_reactions(app, i); return; }
    int count = 0;
    const Message *msgs = tui_app_messages(app, &count);
    messaging_manager_react(app->deps.messaging, msgs[i].id, args);
}

static void cmd_reply(TuiApp *app, const char *args) {
    (void)args;
    int i = last_message(app, 1);
    if (i < 0) tui_app_toast(app, "No message to reply to", 1);
    else tui_app_start_reply(app, i);
}

static void cmd_emoji(TuiApp *app, const char *args) {
    tui_app_open_emoji(app, EMOJI_PICKER_FOR_INPUT, NULL, args);
}

static void cmd_clear(TuiApp *app, const char *args) {
    (void)args;
    const char *jid = messaging_manager_open_jid(app->deps.messaging);
    composer_view_clear(&app->composer);
    app->attachment[0] = '\0';
    tui_app_cancel_reply(app);
    if (jid[0]) messaging_manager_save_draft(app->deps.messaging, jid, "");
}

static const SlashCommand COMMANDS[] = {
    { "help",        "",                     "commands and keyboard shortcuts", cmd_help },
    { "search",      "<text>",               "search messages in every chat",   cmd_search },
    { "reply",       "",                     "reply to the last message",       cmd_reply },
    { "react",       "[emoji]",              "react to the last message",       cmd_react },
    { "attach",      "[path]",               "send a photo, video or file",     cmd_attach },
    { "paste",       "",                     "attach the picture on the clipboard", cmd_paste },
    { "camera",      "",                     "take a photo with the camera",    cmd_camera },
    { "photo",       "",                     "take a photo (same as /camera)",  cmd_camera },
    { "emoji",       "[search]",             "insert an emoji",                 cmd_emoji },
    { "voice",       "",                     "record a voice note",             cmd_voice },
    { "mute",        "[8h|1w|always]",       "mute this chat",                  cmd_mute },
    { "unmute",      "",                     "unmute this chat",                cmd_unmute },
    { "pin",         "",                     "pin this chat",                   cmd_pin },
    { "unpin",       "",                     "unpin this chat",                 cmd_unpin },
    { "archive",     "",                     "move this chat to Archived",      cmd_archive },
    { "unarchive",   "",                     "move this chat back to chats",    cmd_unarchive },
    { "theme",       "[id|default]",         "theme for this chat",             cmd_theme },
    { "tone",        "[file|none|default]",  "notification sound for this chat", cmd_tone },
    { "dnd",         "",                     "toggle do not disturb",           cmd_dnd },
    { "screensaver", "",                     "start the screensaver now",       cmd_lock },
    { "lock",        "",                     "start the screensaver now",       cmd_lock },
    { "softlock",    "",                     "blur this chat, or show it again", cmd_softlock },
    { "transcripts", "",                     "show voice note transcripts in this chat: as the setting says, always, never", cmd_transcripts },
    { "info",        "",                     "contact or group details",        cmd_info },
    { "profile",     "",                     "your name, about and photo",      cmd_profile },
    { "status",      "",                     "post a status",                   cmd_status },
    { "statuses",    "",                     "see your and your contacts' statuses", cmd_statuses },
    { "later",       "<when> <text>",        "send a message later: 18:00, +30m, tomorrow 9:00", cmd_later },
    { "scheduled",   "",                     "messages waiting to be sent later", cmd_scheduled },
    { "agents",      "",                     "the Agentic tab: agents' requests, who is connected, the log (F3)", cmd_agents },
    { "clear",       "",                     "clear the input and draft",       cmd_clear },
    { "settings",    "",                     "open settings",                   cmd_settings },
    { "quit",        "",                     "quit tawk",                       cmd_quit },
    { "exit",        "",                     "quit tawk (same as /quit)",       cmd_quit },
};

#define COMMAND_COUNT ((int)(sizeof(COMMANDS) / sizeof(COMMANDS[0])))

const SlashCommand *tui_commands_all(int *count) {
    *count = COMMAND_COUNT;
    return COMMANDS;
}

int tui_commands_run(TuiApp *app, const char *line) {
    if (!line || line[0] != '/' || line[1] == '/') return 0;
    char name[32];
    size_t n = 0;
    const char *p = line + 1;
    while (*p && !isspace((unsigned char)*p) && n + 1 < sizeof(name)) name[n++] = (char)tolower((unsigned char)*p++);
    name[n] = '\0';
    while (isspace((unsigned char)*p)) p++;
    static char args[16 * 1024 + 1];                    /* room for a whole message (/later) */
    str_copy(args, sizeof(args), p);
    str_trim(args);
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(COMMANDS[i].name, name) == 0) { COMMANDS[i].run(app, args); return 1; }
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "Unknown command /%s; type /help", name);
    tui_app_toast(app, msg, 1);
    return 1;
}
