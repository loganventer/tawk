/* Composition root: builds every concrete component, injects them through
 * their contracts, runs the terminal client, and tears everything down in
 * reverse order. No other file knows which implementations are in use. */

#include "clients/cli/backup_command.h"
#include "clients/cli/control_command.h"
#include "clients/control/control_server.h"
#include "clients/cli/database_crypt_command.h"
#include "clients/cli/doctor.h"
#include "clients/cli/updater.h"
#include "clients/tui/approval_queue.h"
#include "clients/tui/blink_state.h"
#include "clients/tui/title_flasher.h"
#include "clients/tui/tui_app.h"
#include "clients/tui/tui_notifier.h"
#include "composition/account_host.h"
#include "composition/backend_gateway_factory.h"
#include "infrastructure/audio/audio_backend_factory.h"
#include "infrastructure/composite_notifier.h"
#include "infrastructure/ffmpeg_camera.h"
#include "infrastructure/ffmpeg_video_poster.h"
#include "infrastructure/ifaddrs_network_monitor.h"
#include "infrastructure/media_cache_janitor.h"
#include "infrastructure/osc52_clipboard.h"
#include "infrastructure/osc_terminal_title.h"
#include "infrastructure/pipeline_audio_recorder.h"
#include "infrastructure/poppler_document_pages.h"
#include "infrastructure/process_audio_player.h"
#include "infrastructure/pty_idle_action.h"
#include "infrastructure/sound_notifier.h"
#include "infrastructure/system_clipboard_image.h"
#include "infrastructure/system_media_opener.h"
#include "infrastructure/openssl_file_cipher.h"
#include "infrastructure/tar_archive.h"
#include "infrastructure/unix_control_client.h"
#include "infrastructure/unix_control_transport.h"
#include "infrastructure/terminal_passphrase_prompt.h"
#include "infrastructure/terminal_graphics.h"
#include "managers/account_manager.h"
#include "managers/account_roster_manager.h"
#include "managers/automation_manager.h"
#include "managers/media_manager.h"
#include "managers/messaging_manager.h"
#include "managers/backup_manager.h"
#include "managers/call_manager.h"
#include "managers/database_crypt_manager.h"
#include "managers/composite_event_observer.h"
#include "managers/profile_manager.h"
#include "managers/settings_manager.h"
#include "managers/scheduling_manager.h"
#include "managers/status_feed_manager.h"
#include "managers/status_manager.h"
#include "resource_access/caching_contact_store.h"
#include "resource_access/caching_message_store.h"
#include "resource_access/ini_settings_store.h"
#include "resource_access/json_theme_repository.h"
#include "resource_access/sidecar_gateway.h"
#include "resource_access/sqlite_account_store.h"
#include "resource_access/sqlite_chat_prefs_store.h"
#include "resource_access/file_admin_token_store.h"
#include "resource_access/sqlite_automation_log.h"
#include "resource_access/sqlite_chat_store.h"
#include "resource_access/sqlite_contact_store.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_database_crypt.h"
#include "resource_access/sqlite_file_snapshot.h"
#include "resource_access/sqlite_key.h"
#include "resource_access/sqlite_jid_alias_store.h"
#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_receipt_store.h"
#include "resource_access/tsv_emoji_catalog.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_profile_store.h"
#include "resource_access/sqlite_scheduled_message_store.h"
#include "resource_access/sqlite_status_store.h"
#include "resource_access/text_chat_exporter.h"
#include "resource_access/whatsmeow_gateway.h"
#include "utilities/app_info.h"
#include "utilities/admin_token_path.h"
#include "utilities/control_socket_path.h"
#include "utilities/event_queue.h"
#include "utilities/log.h"
#include "utilities/instance_lock.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"

#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif

#ifndef APP_SHARE_DIR
#define APP_SHARE_DIR "/usr/local/share/" APP_NAME
#endif

static volatile sig_atomic_t s_quit = 0;
static volatile sig_atomic_t s_restart = 0;   /* the client asked to start again (backend switch) */

static void on_signal(int sig) {
    (void)sig;
    s_quit = 1;
}

typedef struct Options {
    const char *config_path;
    const char *backend;
    int         debug;
    int         doctor;
    int         update;
    int         reinstall;
    int         yes;
    DatabaseCryptAction crypt;      /* --encrypt, --decrypt or --change-passphrase */
    const char *backup_path;        /* --backup FILE */
    const char *restore_path;       /* --restore FILE */
    int         with_media;
    int         with_login;
} Options;

static void usage(void) {
    printf("%s %s: WhatsApp in your terminal\n\n"
           "Usage: %s [options]\n"
           "       %s send CHAT TEXT...  (or - to read the text from standard input)\n"
           "       %s tail [CHAT...] [--json]\n"
           "       %s unread [--json]\n"
           "       %s status-line [--format '{unread} {mentions} {chats}']\n"
           "       The commands talk to a running tawk with Settings > Automation > Agent access on.\n"
           "       With several accounts, --account NAME picks one; unread and status-line cover all without it.\n\n"
           "  --config PATH     use this config file (default ~/.config/%s/config.ini)\n"
           "  --backend NAME    whatsmeow (in-process) or baileys (Node.js sidecar)\n"
           "  --debug           verbose logging to the log file\n"
           "  --doctor          check the setup and say what is missing\n"
           "  --update [--yes]  install the newest version from GitHub, if there is one\n"
           "  --reinstall       install the newest version again, even when up to date\n"
           "  --encrypt         encrypt your chats with a passphrase asked at every start\n"
           "  --decrypt         remove the encryption again\n"
           "  --change-passphrase  change the passphrase of encrypted chats\n"
           "  --backup FILE [--with-media] [--with-login]\n"
           "                    write an encrypted backup of your chats, settings and themes\n"
           "  --restore FILE [--yes]  restore a backup (what it replaces is kept aside)\n"
           "  --version         print the version\n"
           "  --help            show this help\n\n"
           "%s by %s <%s>, %s\n",
           APP_NAME, APP_VERSION, APP_NAME, APP_NAME, APP_NAME, APP_NAME, APP_NAME, APP_NAME, APP_NAME, APP_AUTHOR, APP_AUTHOR_EMAIL, APP_HOMEPAGE);
}

static int parse_args(int argc, char **argv, Options *o) {
    memset(o, 0, sizeof(*o));
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 1; }
        if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
            printf("%s %s%s%.7s%s\nby %s <%s>\n%s\n", APP_NAME, APP_VERSION, APP_COMMIT[0] ? " (" : "", APP_COMMIT,
                   APP_COMMIT[0] ? ")" : "", APP_AUTHOR, APP_AUTHOR_EMAIL, APP_HOMEPAGE);
            return 1;
        }
        if (!strcmp(argv[i], "--debug")) { o->debug = 1; continue; }
        if (!strcmp(argv[i], "--doctor")) { o->doctor = 1; continue; }
        if (!strcmp(argv[i], "--update")) { o->update = 1; continue; }
        if (!strcmp(argv[i], "--reinstall")) { o->update = o->reinstall = 1; continue; }
        if (!strcmp(argv[i], "--yes") || !strcmp(argv[i], "-y")) { o->yes = 1; continue; }
        if (!strcmp(argv[i], "--encrypt")) { o->crypt = DATABASE_CRYPT_ENCRYPT; continue; }
        if (!strcmp(argv[i], "--decrypt")) { o->crypt = DATABASE_CRYPT_DECRYPT; continue; }
        if (!strcmp(argv[i], "--change-passphrase")) { o->crypt = DATABASE_CRYPT_CHANGE; continue; }
        if (!strcmp(argv[i], "--backup") && i + 1 < argc) { o->backup_path = argv[++i]; continue; }
        if (!strcmp(argv[i], "--restore") && i + 1 < argc) { o->restore_path = argv[++i]; continue; }
        if (!strcmp(argv[i], "--with-media")) { o->with_media = 1; continue; }
        if (!strcmp(argv[i], "--with-login")) { o->with_login = 1; continue; }
        if (!strcmp(argv[i], "--config") && i + 1 < argc) { o->config_path = argv[++i]; continue; }
        if (!strcmp(argv[i], "--backend") && i + 1 < argc) { o->backend = argv[++i]; continue; }
        fprintf(stderr, "%s: unknown option %s (see --help)\n", APP_NAME, argv[i]);
        return 2;
    }
    return 0;
}

/* Prefers the installed location, falling back to a copy next to the
 * current directory so the app also runs straight from a source checkout. */
static void locate(const char *installed, const char *local, const char *probe, char *out, size_t size) {
    char check[1024];
    snprintf(check, sizeof(check), "%s%s", installed, probe);
    if (access(check, F_OK) == 0) { str_copy(out, size, installed); return; }
    snprintf(check, sizeof(check), "%s%s", local, probe);
    str_copy(out, size, access(check, F_OK) == 0 ? local : installed);
}

static int whatsmeow_available(void) {
#ifdef APP_WITH_WHATSMEOW
    return 1;
#else
    return 0;
#endif
}

/* Where this binary lives, falling back to argv[0]. */
static void self_path(const char *argv0, char *self, size_t size) {
#ifdef __APPLE__
    uint32_t len = (uint32_t)size;
    if (_NSGetExecutablePath(self, &len) != 0) str_copy(self, size, argv0);
    char real[1024];
    if (realpath(self, real)) str_copy(self, size, real);
#else
    ssize_t n = readlink("/proc/self/exe", self, size - 1);
    if (n > 0) self[n] = '\0';
    else str_copy(self, size, argv0);
#endif
}

/* Starts tawk again in place of this process, after a clean shutdown. A
 * --backend option is dropped so the backend saved in the settings (which
 * is what the restart is for) takes effect. Returns only on failure. */
static void restart_self(int argc, char **argv) {
    char **args = calloc((size_t)argc + 1, sizeof(*args));
    if (!args) return;
    int n = 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && !strcmp(argv[i], "--backend")) { i++; continue; }
        args[n++] = argv[i];
    }
    args[n] = NULL;
    char self[1024];
    self_path(argv[0], self, sizeof(self));
    execv(self, args);
    execvp(argv[0], args);
    free(args);
}

/* `tawk --update`: reinstalls from GitHub where this binary lives. */
static int run_update(const Options *opt, const char *argv0) {
    char self[1024];
    self_path(argv0, self, sizeof(self));
    Settings s;
    settings_set_defaults(&s);
#ifdef APP_WITH_WHATSMEOW
    int whatsmeow_built = 1;
#else
    int whatsmeow_built = 0;
#endif
    UpdaterOptions u = { opt->yes, opt->reinstall, self, s.sidecar_dir, whatsmeow_built };
    return updater_run(&u);
}

/* `tawk --doctor`: checks the setup without touching the database or the network. */
static int run_doctor(const Options *opt, const Settings *s, SettingsManager *settings_mgr, ISettingsStore *store,
                      IThemeRepository *themes, IEmojiCatalog *emoji, const char *themes_dir,
                      const char *db_path, const char *log_path) {
    char sidecar_dir[512];
    locate(s->sidecar_dir, "sidecar", "/src/index.js", sidecar_dir, sizeof(sidecar_dir));
    IAudioBackend *audio = audio_backend_factory_create(s->audio_backend);
#ifdef APP_WITH_WHATSMEOW
    int whatsmeow_built = 1;
#else
    int whatsmeow_built = 0;
#endif
    DoctorInputs in = {
        s, opt->config_path ? opt->config_path : s->config_path, db_path, log_path, themes_dir, sidecar_dir,
        emoji, audio, whatsmeow_built, opt->backend ? opt->backend : s->backend,
        sqlite_key_supported(), sqlite_database_is_encrypted(db_path),
    };
    int rc = doctor_run(&in);
    audio->destroy(audio);
    settings_manager_destroy(settings_mgr);
    themes->destroy(themes);
    emoji->destroy(emoji);
    store->destroy(store);
    return rc;
}

/* `tawk --backup FILE` and `tawk --restore FILE`, under the instance lock. */
static int run_backup_or_restore(const Options *opt, const Settings *s, const char *config_path, const char *themes_dir,
                                 const char *db_path, const char *auth_dir, IPassphrasePrompt *prompt) {
    IDatabaseSnapshot *snapshot = sqlite_file_snapshot_create();
    IArchive *archive = tar_archive_create();
    IFileCipher *file_cipher = openssl_file_cipher_create();
    BackupManagerDeps deps = { snapshot, archive, file_cipher, sqlite_database_is_encrypted(db_path) };
    BackupManager *backups = backup_manager_create(&deps);
    char accounts_dir[600];
    path_join(accounts_dir, sizeof(accounts_dir), s->data_dir, "accounts");
    BackupPaths paths = { s->data_dir, db_path, config_path, themes_dir, s->media_dir, auth_dir, accounts_dir };
    int rc;
    if (opt->backup_path) {
        BackupRequest request = { paths, opt->backup_path, opt->with_media, opt->with_login };
        rc = backup_command_run(backups, prompt, &request);
    } else {
        rc = restore_command_run(backups, prompt, &paths, opt->restore_path, opt->yes);
    }
    backup_manager_destroy(backups);
    file_cipher->destroy(file_cipher);
    archive->destroy(archive);
    snapshot->destroy(snapshot);
    return rc;
}

/* tawk send, tail, unread and status-line talk to the tawk already
 * running, so they need no terminal, lock or database of their own. */
static int run_control_command(ControlCommandKind kind, int argc, char **argv) {
    setlocale(LC_ALL, "");
    signal(SIGPIPE, SIG_IGN);
    ControlOptions options;
    if (control_options_parse(kind, argc, argv, &options) != 0) return CONTROL_EXIT_FAILED;
    char path[600];
    control_socket_path(path, sizeof(path));
    IControlClient *client = unix_control_client_create();
    if (!client) return CONTROL_EXIT_FAILED;
    int rc = control_command_run(&options, client, path);
    client->destroy(client);
    return rc;
}

int main(int argc, char **argv) {
    ControlCommandKind command = argc > 1 ? control_command_kind_of(argv[1]) : CONTROL_COMMAND_NONE;
    if (command != CONTROL_COMMAND_NONE) return run_control_command(command, argc - 2, argv + 2);
    Options opt;
    int rc = parse_args(argc, argv, &opt);
    if (rc) return rc == 1 ? 0 : rc;

    setlocale(LC_ALL, "");
    if (opt.update) return run_update(&opt, argv[0]);
    umask(077);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    if (!opt.doctor && (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))) {
        fprintf(stderr, "%s needs an interactive terminal\n", APP_NAME);
        return 2;
    }

    /* Settings and themes */
    char config_dir[512], user_themes[600], bundled_themes[600], emoji_dir[600];
    path_config_dir(config_dir, sizeof(config_dir));
    path_join(user_themes, sizeof(user_themes), config_dir, "themes");
    if (!opt.doctor) {                          /* the setup check never writes anything */
        path_mkdir_p(config_dir, 0700);
        path_mkdir_p(user_themes, 0700);
    }
    locate(APP_SHARE_DIR "/themes", "themes", "/whatsapp-dark.json", bundled_themes, sizeof(bundled_themes));
    locate(APP_SHARE_DIR "/emoji", "assets/emoji", "/emoji.tsv", emoji_dir, sizeof(emoji_dir));
    char emoji_file[700];
    path_join(emoji_file, sizeof(emoji_file), emoji_dir, "emoji.tsv");
    IEmojiCatalog *emoji = tsv_emoji_catalog_create(emoji_file);

    Settings defaults;
    settings_set_defaults(&defaults);
    const char *config_path = opt.config_path ? opt.config_path : defaults.config_path;
    ISettingsStore *settings_store = opt.doctor ? ini_settings_store_create_read_only(config_path)
                                                : ini_settings_store_create(config_path);
    IThemeRepository *themes = json_theme_repository_create(bundled_themes, user_themes);
    SettingsManager *settings_mgr = settings_manager_create(settings_store, themes);
    settings_manager_load(settings_mgr);
    const Settings *s = settings_manager_current(settings_mgr);

    /* Data folders and logging */
    char auth_dir[600], db_path[600], log_path[600], state_dir[512];
    settings_auth_dir(s, auth_dir, sizeof(auth_dir));
    settings_db_path(s, db_path, sizeof(db_path));
    settings_log_path(s, log_path, sizeof(log_path));
    if (opt.doctor) return run_doctor(&opt, s, settings_mgr, settings_store, themes, emoji, bundled_themes, db_path, log_path);
    path_state_dir(state_dir, sizeof(state_dir));
    path_mkdir_p(state_dir, 0700);
    path_mkdir_p(s->data_dir, 0700);
    path_mkdir_p(auth_dir, 0700);
    path_mkdir_p(s->media_dir, 0700);
    log_open(log_path, opt.debug ? LOG_LEVEL_DEBUG : log_level_parse(s->log_level));
    LOG_INFO("%s %s starting", APP_NAME, APP_VERSION);
    media_cache_janitor_prune(s->media_dir, s->media_cache_mb);

    /* One tawk per data folder, and the passphrase of encrypted chats. Both
     * come before the database is opened, and before the screen is taken over. */
    IDatabaseCipher *cipher = sqlite_database_cipher_create();
    DatabaseCryptManagerDeps crypt_deps = { cipher, db_path };
    DatabaseCryptManager *crypt = database_crypt_manager_create(&crypt_deps);
    IPassphrasePrompt *prompt = terminal_passphrase_prompt_create();
    InstanceLock lock;
    if (instance_lock_acquire(&lock, s->data_dir) != 0) {
        fprintf(stderr, "%s: another %s is already using %s; quit it first\n", APP_NAME, APP_NAME, s->data_dir);
        return 1;
    }
    if (opt.crypt || opt.backup_path || opt.restore_path) {
        int cli_rc = 1;
        if (opt.crypt) {
            cli_rc = database_crypt_command_run(opt.crypt, crypt, prompt, opt.yes);
        } else {
            cli_rc = run_backup_or_restore(&opt, s, config_path, user_themes, db_path, auth_dir, prompt);
        }
        instance_lock_release(&lock);
        prompt->destroy(prompt);
        database_crypt_manager_destroy(crypt);
        cipher->destroy(cipher);
        return cli_rc;
    }
    Passphrase key;
    int unlocked = database_unlock(crypt, prompt, &key);
    if (unlocked < 0) return 1;

    /* Storage: SQLite behind caching decorators */
    sqlite3 *db = sqlite_database_open(db_path, unlocked ? &key : NULL);
    passphrase_wipe(&key);
    if (!db) {
        fprintf(stderr, "%s: cannot open %s (see %s)\n", APP_NAME, db_path, log_path);
        return 1;
    }
    IAccountStore *account_store = sqlite_account_store_create(db);
    IChatPrefsStore *chat_prefs = sqlite_chat_prefs_store_create(db);
    AccountRosterManagerDeps roster_deps = { account_store, chat_prefs };
    AccountRosterManager *roster = account_roster_manager_create(&roster_deps);
    IChatExporter *exporter = text_chat_exporter_create();

    /* Which backend the accounts run on, and where its parts are */
    const char *backend = opt.backend ? opt.backend : s->backend;
    char sidecar_dir[512];
    locate(s->sidecar_dir, "sidecar", "/src/index.js", sidecar_dir, sizeof(sidecar_dir));

    /* Audio, media and terminal integration */
    IAudioBackend *audio = audio_backend_factory_create(s->audio_backend);
    IAudioPlayer *voice_player = process_audio_player_create(audio);
    IAudioPlayer *sound_player = process_audio_player_create(audio);
    IAudioRecorder *recorder = pipeline_audio_recorder_create(audio);
    IMediaOpener *opener = system_media_opener_create(s);
    IIdleAction *screensaver = pty_idle_action_create();
    ITerminalTitle *title = osc_terminal_title_create();
    IClipboard *clipboard = osc52_clipboard_create();
    IVideoPoster *posters = ffmpeg_video_poster_create();
    IDocumentPages *pages = poppler_document_pages_create();
    IClipboardImage *clipboard_image = system_clipboard_image_create();
    BlinkState blink;
    memset(&blink, 0, sizeof(blink));
    TitleFlasher flasher;
    title_flasher_init(&flasher, title);

    INotifier *notifier = composite_notifier_create();
    composite_notifier_add(notifier, sound_notifier_create(sound_player, s));
    composite_notifier_add(notifier, tui_notifier_create(&blink, &flasher, title, s));

    /* The accounts: each gets its gateway, its stores over the one database
     * and its managers. The clients start with the primary one in view. */
    BackendGatewayOptions gateway_options = { s, backend, sidecar_dir, state_dir, opt.debug };
    IGatewayFactory *gateways = backend_gateway_factory_create(&gateway_options);
    AccountRuntimeParams runtime_params = { db, s, notifier, exporter, gateways };
    AccountHost *host = account_host_create(&runtime_params, account_store);
    IAccountDirectory *directory = host ? account_host_directory(host) : NULL;
    const AccountServices *active = directory ? directory->find(directory, account_roster_manager_primary(roster)) : NULL;
    if (!active && directory) active = directory->at(directory, 0);
    if (!active) {
        fprintf(stderr, "%s: no account could be started (see %s)\n", APP_NAME, log_path);
        return 1;
    }
    /* Agents: the control socket, its rules and log, and the requests waiting for you */
    IAutomationLog *automation_log = sqlite_automation_log_create(db);
    char admin_token_file[600];
    admin_token_path(admin_token_file, sizeof(admin_token_file));
    IAdminTokenStore *admin_tokens = file_admin_token_store_create(admin_token_file);
    AutomationManagerDeps automation_deps = { automation_log, s, admin_tokens };
    AutomationManager *automation = automation_manager_create(&automation_deps);
    ApprovalQueue *approvals = approval_queue_create();
    IControlTransport *control_transport = unix_control_transport_create();
    char control_path[600];
    control_socket_path(control_path, sizeof(control_path));
    ControlServerDeps control_deps = { control_transport, approval_queue_prompt(approvals), active->messaging, active->profiles,
                                       active->scheduling, active->feed, automation, settings_mgr, active->accounts,
                                       active->statuses, active->calls, active->backend_name, control_path,
                                       directory, roster };
    ControlServer *control = control_server_create(&control_deps);
    ICamera *camera = ffmpeg_camera_create();
    MediaManagerDeps media_deps = { opener, voice_player, recorder, camera, audio, s };
    MediaManager *media = media_manager_create(&media_deps);

    /* Client */
    int cell_w, cell_h;
    terminal_graphics_cell_pixels(&cell_w, &cell_h);
    TuiAppDeps tdeps = {
        .messaging = active->messaging, .profiles = active->profiles, .calls = active->calls, .media = media,
        .settings = settings_mgr, .screensaver = screensaver, .sound_player = sound_player, .emoji = emoji,
        .clipboard = clipboard, .video_posters = posters, .document_pages = pages, .clipboard_image = clipboard_image,
        .notifier = notifier, .blink = &blink, .title = &flasher,
        .backend_name = active->backend_name, .audio_backend_name = audio->name(audio), .user_theme_dir = user_themes,
        .sixel_supported = terminal_graphics_sixel(), .cell_width_px = cell_w, .cell_height_px = cell_h,
        .quit_requested = &s_quit,
        .accounts = active->accounts, .statuses = active->statuses, .whatsmeow_available = whatsmeow_available(),
        .restart_requested = &s_restart, .feed = active->feed, .scheduling = active->scheduling,
        .automation = automation, .approvals = approvals, .frame_hook = control ? control_server_frame_hook(control) : NULL,
        .directory = directory, .roster = roster, .active_account = active->id,
    };
    TuiApp *tui = tui_app_create(&tdeps);
    int exit_code = tui ? tui_app_run(tui) : 1;

    /* Teardown, reverse order. Closing the queue first unblocks a backend
     * that is waiting on back-pressure so it can shut down. */
    tui_app_destroy(tui);
    control_server_destroy(control);
    if (control_transport) control_transport->destroy(control_transport);
    approval_queue_destroy(approvals);
    automation_manager_destroy(automation);
    if (admin_tokens) admin_tokens->destroy(admin_tokens);
    if (automation_log) automation_log->destroy(automation_log);
    media_manager_destroy(media);
    account_host_destroy(host);
    if (gateways) gateways->destroy(gateways);
    notifier->destroy(notifier);
    title->destroy(title);
    clipboard->destroy(clipboard);
    posters->destroy(posters);
    pages->destroy(pages);
    clipboard_image->destroy(clipboard_image);
    screensaver->destroy(screensaver);
    opener->destroy(opener);
    recorder->destroy(recorder);
    if (camera) camera->destroy(camera);
    sound_player->destroy(sound_player);
    voice_player->destroy(voice_player);
    audio->destroy(audio);
    exporter->destroy(exporter);
    account_roster_manager_destroy(roster);
    chat_prefs->destroy(chat_prefs);
    account_store->destroy(account_store);
    sqlite_database_close(db);
    settings_manager_destroy(settings_mgr);
    themes->destroy(themes);
    emoji->destroy(emoji);
    settings_store->destroy(settings_store);
    prompt->destroy(prompt);
    database_crypt_manager_destroy(crypt);
    cipher->destroy(cipher);
    instance_lock_release(&lock);
    LOG_INFO("%s stopped", APP_NAME);
    log_close();
    if (s_restart) restart_self(argc, argv);
    return exit_code;
}
