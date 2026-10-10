#include "core/settings.h"
#include "utilities/app_info.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

/* Install locations (FHS): read-only data in share/, the internal Node.js
 * program in lib/. Overridden by the Makefile from PREFIX. */
#ifndef APP_SHARE_DIR
#define APP_SHARE_DIR "/usr/local/share/" APP_NAME
#endif
#ifndef APP_LIB_DIR
#define APP_LIB_DIR "/usr/local/lib/" APP_NAME
#endif

void settings_set_defaults(Settings *s) {
    memset(s, 0, sizeof(*s));
    str_copy(s->theme, sizeof(s->theme), SETTINGS_DEFAULT_THEME);
    s->sidebar_width = 34;
    str_copy(s->chat_list_style, sizeof(s->chat_list_style), "detailed");
    s->chat_spacing = 1;
    s->convert_emoticons = 1;
    s->reopen_last_chat = 1;
    s->merge_accounts = 1;
    s->use_24h_clock = 1;
    s->splash = 1;
    s->format_text = 1;
    s->mention_notifications = 1;
    s->status_keep_days = 30;
    s->mouse = 1;
    s->inline_thumbnails = 1;
    s->portraits = 1;
    str_copy(s->image_mode, sizeof(s->image_mode), "auto");

    s->enter_sends = 1;
    s->message_margin = 50;
    s->send_read_receipts = 1;
    s->share_typing = 1;
    s->appear_online = 1;
    s->show_online = 1;
    s->show_transcripts = 1;
    s->tldr_min_chars = 0;
    s->tldr_back_days = 30;
    str_copy(s->recent_emoji, sizeof(s->recent_emoji), "\xF0\x9F\x91\x8D \xE2\x9D\xA4\xEF\xB8\x8F \xF0\x9F\x98\x82 \xF0\x9F\x98\xAE \xF0\x9F\x98\xA2 \xF0\x9F\x99\x8F");

    s->notifications = 1;
    s->group_notifications = 1;
    s->show_preview = 1;
    s->sound = 1;
    str_copy(s->sound_file, sizeof(s->sound_file), APP_SHARE_DIR "/sounds/notify.wav");
    s->blink = 1;
    s->blink_seconds = 8;
    s->title_flash = 1;
    s->terminal_bell = 0;
    s->screen_flash = 0;

    s->auto_download_media = 1;
    s->auto_download_max_mb = 16;
    s->media_cache_mb = 1024;

    str_copy(s->audio_backend, sizeof(s->audio_backend), "auto");
    str_copy(s->mic_device, sizeof(s->mic_device), "default");
    s->voice_max_seconds = 300;

    s->screensaver = 1;
    s->idle_minutes = 5;
    str_copy(s->screensaver_command, sizeof(s->screensaver_command), "matrix-clock");
    s->wake_on_message = 1;

    s->backoff_initial_ms = 1000;
    s->backoff_max_ms = 60000;
    s->breaker_threshold = 5;
    s->breaker_cooldown_s = 120;

    str_copy(s->automation_access, sizeof(s->automation_access), "read");
    s->automation_rate = 5;
    s->automation_self_per_hour = 20;
    s->owner_replies_per_hour = 60;
    s->owner_approvals = 1;
    s->owner_card_wait = 20;
    str_copy(s->automation_disclaimer_text, sizeof(s->automation_disclaimer_text), "\xF0\x9F\xA4\x96 Created with my AI assistant");
    s->automation_push_received = 1;
    s->automation_push_sent = 1;
    str_copy(s->transcribe_model, sizeof(s->transcribe_model), "large-v3-turbo");
    str_copy(s->transcribe_languages, sizeof(s->transcribe_languages), "en");

    str_copy(s->backend, sizeof(s->backend), "whatsmeow");
    /* Per-user files (XDG): chats and login in data, downloads in cache. */
    path_data_dir(s->data_dir, sizeof(s->data_dir));
    char cache[512];
    path_cache_dir(cache, sizeof(cache));
    path_join(s->media_dir, sizeof(s->media_dir), cache, "media");
    str_copy(s->sidecar_dir, sizeof(s->sidecar_dir), APP_LIB_DIR "/sidecar");
    str_copy(s->node_binary, sizeof(s->node_binary), "node");
    str_copy(s->log_level, sizeof(s->log_level), "info");

    char dir[512];
    path_config_dir(dir, sizeof(dir));
    path_join(s->config_path, sizeof(s->config_path), dir, "config.ini");
}

void settings_db_path(const Settings *s, char *out, unsigned long size)   { path_join(out, size, s->data_dir, APP_NAME ".db"); }
void settings_auth_dir(const Settings *s, char *out, unsigned long size)  { path_join(out, size, s->data_dir, "auth"); }
void settings_log_path(const Settings *s, char *out, unsigned long size) {
    (void)s;
    char state[512];
    path_state_dir(state, sizeof(state));
    path_join(out, size, state, APP_NAME ".log");
}
