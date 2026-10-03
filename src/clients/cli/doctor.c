#include "clients/cli/doctor.h"
#include "utilities/app_info.h"
#include "utilities/path_util.h"
#include "utilities/platform.h"
#include "utilities/process_util.h"

#include <ctype.h>
#include <dirent.h>
#include <langinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum { OK, WARN, FAIL } Level;

typedef struct Tally { int warnings, failures; } Tally;

static void line(Tally *t, Level level, const char *label, const char *detail, const char *hint) {
    int colour = isatty(STDOUT_FILENO);
    static const char *const MARK[] = { "\xE2\x9C\x93", "!", "\xE2\x9C\x97" };
    static const char *const COLOUR[] = { "\033[32m", "\033[33m", "\033[31m" };
    printf("  %s%s%s %-18s %s\n", colour ? COLOUR[level] : "", MARK[level], colour ? "\033[0m" : "", label, detail);
    if (level != OK && hint && *hint) printf("    %-18s %s\n", "", hint);
    if (level == WARN) t->warnings++;
    if (level == FAIL) t->failures++;
}

static void section(const char *title) { printf("\n%s\n", title); }

static int is_dir(const char *p)  { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int is_file(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }

/* Writable, or missing but creatable (the nearest existing parent is writable). */
static int writable(const char *path) {
    char dir[600];
    snprintf(dir, sizeof(dir), "%s", path);
    while (dir[0] && access(dir, F_OK) != 0) {
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) { snprintf(dir, sizeof(dir), "/"); break; }
        *slash = '\0';
    }
    return access(dir, W_OK) == 0;
}

static int count_json(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len > 5 && strcmp(e->d_name + len - 5, ".json") == 0) n++;
    }
    closedir(d);
    return n;
}

/* The first word of a command line. */
static void program_of(const char *command, char *out, size_t size) {
    size_t n = 0;
    while (*command && isspace((unsigned char)*command)) command++;
    while (*command && !isspace((unsigned char)*command) && n + 1 < size) out[n++] = *command++;
    out[n] = '\0';
}

/* The package that provides a tool, for the hint (apt names; close enough elsewhere). */
static const char *install_hint(const char *what) {
    static char hint[160];
    const char *mgr = platform_is_macos() ? "brew install" : process_on_path("apt-get") ? "sudo apt install"
                    : process_on_path("dnf") ? "sudo dnf install" : process_on_path("pacman") ? "sudo pacman -S"
                    : process_on_path("zypper") ? "sudo zypper install" : "install";
    snprintf(hint, sizeof(hint), "%s %s", mgr, what);
    return hint;
}

static void check_terminal(Tally *t) {
    section("Terminal");
    const char *term = getenv("TERM");
    line(t, term && *term && strcmp(term, "dumb") != 0 ? OK : FAIL, "TERM", term && *term ? term : "not set",
         "run tawk inside a terminal emulator");
    const char *codeset = nl_langinfo(CODESET);
    int utf8 = codeset && (strcasecmp(codeset, "UTF-8") == 0 || strcasecmp(codeset, "utf8") == 0);
    line(t, utf8 ? OK : WARN, "UTF-8 locale", codeset ? codeset : "unknown",
         "set LANG to a UTF-8 locale (for example en_GB.UTF-8) so emoji and borders draw correctly");
    const char *colorterm = getenv("COLORTERM");
    int rich = (colorterm && *colorterm) || (term && strstr(term, "256"));
    line(t, rich ? OK : WARN, "colours", rich ? (colorterm && *colorterm ? colorterm : "256 colours") : "basic",
         "themes look best with TERM=xterm-256color or a true-colour terminal");
}

static void check_files(Tally *t, const DoctorInputs *in) {
    section("Files");
    const Settings *s = in->settings;
    if (is_file(in->config_path)) {
        line(t, s->config_trusted ? OK : WARN, "config", in->config_path,
             "chmod 600 the config file; commands in it are ignored while others can write to it");
    } else {
        line(t, OK, "config", "not created yet (defaults are written on first run)", NULL);
    }
    line(t, writable(s->data_dir) ? OK : FAIL, "data", s->data_dir, "the folder must be writable");
    line(t, OK, "database", is_file(in->db_path) ? in->db_path : "created on first run", NULL);
    if (in->db_encrypted) {
        line(t, in->sqlcipher_built ? OK : FAIL, "encryption", "your chats are encrypted with a passphrase",
             "this build has no SQLCipher to open them; install SQLCipher and build again");
    } else {
        line(t, OK, "encryption", in->sqlcipher_built ? "off (tawk --encrypt turns it on)" : "not available (built without SQLCipher)", NULL);
    }
    int openssl = process_on_path("openssl");
    line(t, openssl ? OK : WARN, "backups", openssl ? "openssl found (tawk --backup)" : "openssl not found",
         install_hint("openssl"));
    line(t, OK, "log", in->log_path, NULL);
    char buf[640];
    int themes = count_json(in->themes_dir);
    snprintf(buf, sizeof(buf), "%d in %s", themes, in->themes_dir);
    line(t, themes ? OK : WARN, "themes", buf, "reinstall tawk; only the built-in colours are available");
    int emoji = in->emoji ? in->emoji->count(in->emoji) : 0;
    snprintf(buf, sizeof(buf), "%d emoji", emoji);
    line(t, emoji ? OK : WARN, "emoji", buf, "reinstall tawk; the emoji picker will be empty");
    if (s->sound) line(t, is_file(s->sound_file) ? OK : WARN, "notify sound", s->sound_file,
                       "set another file under Settings, Notifications");
}

static void check_backend(Tally *t, const DoctorInputs *in) {
    section("WhatsApp backend");
    int wants_baileys = strcmp(in->backend, "baileys") == 0;
    line(t, in->whatsmeow_built ? OK : (wants_baileys ? OK : WARN), "whatsmeow",
         in->whatsmeow_built ? "built in" : "not built in", "rebuild with Go installed, or use the Node.js backend");
    int sidecar_needed = wants_baileys || !in->whatsmeow_built;
    char index[700], modules[700];
    snprintf(index, sizeof(index), "%s/src/index.js", in->sidecar_dir);
    snprintf(modules, sizeof(modules), "%s/node_modules", in->sidecar_dir);
    int sidecar_ok = is_file(index) && is_dir(modules);
    int node_ok = process_on_path(in->settings->node_binary);
    Level level = sidecar_needed ? FAIL : OK;
    if (sidecar_needed || sidecar_ok) {
        line(t, sidecar_ok ? OK : level, "baileys sidecar", sidecar_ok ? in->sidecar_dir : "not installed",
             "reinstall with ./install.sh --with-sidecar");
        line(t, node_ok ? OK : level, "node", node_ok ? in->settings->node_binary : "not found", install_hint("nodejs"));
    }
    char active[96];
    snprintf(active, sizeof(active), "%s", !wants_baileys && in->whatsmeow_built ? "whatsmeow (in-process)" : "baileys (Node.js)");
    line(t, OK, "in use", active, NULL);
}

static void check_media(Tally *t, const DoctorInputs *in) {
    section("Voice notes and media");
    int ffmpeg = process_on_path("ffmpeg");
    line(t, ffmpeg ? OK : WARN, "ffmpeg", ffmpeg ? "found" : "not found",
         install_hint("ffmpeg  (needed to record and convert voice notes)"));
    int audio_ok = in->audio && in->audio->available(in->audio);
    char buf[160];
    snprintf(buf, sizeof(buf), "%s%s", in->audio ? in->audio->name(in->audio) : "none", audio_ok ? "" : " (tools missing)");
    const char *audio_hint = platform_is_macos() ? install_hint("ffmpeg")
                           : install_hint("pulseaudio-utils  (or pipewire-bin, or alsa-utils)");
    line(t, audio_ok ? OK : WARN, "audio", buf, audio_hint);
    if (platform_is_macos()) {
        int rec = process_on_path("rec");
        line(t, rec ? OK : WARN, "recorder", rec ? "sox (rec)" : "ffmpeg; recordings may click",
             install_hint("sox  (clean microphone recording)"));
    }

    const char *opener = NULL;
    if (platform_is_macos()) opener = process_on_path("open") ? "open" : NULL;
    else if (platform_is_wsl() && process_on_path("wslview")) opener = "wslview";
    else if (platform_is_wsl() && platform_wsl_interop()) opener = "explorer.exe";
    else if (process_on_path("xdg-open")) opener = "xdg-open";
    else if (process_on_path("gio")) opener = "gio open";
    else if (process_on_path("cygstart")) opener = "cygstart";
    line(t, opener ? OK : WARN, "media viewer", opener ? opener : "none found",
         install_hint("xdg-utils  (opens images and videos in your default viewer)"));

    if (platform_is_wsl()) {
        int interop = platform_wsl_interop();
        line(t, interop ? OK : WARN, "Windows programs", interop ? "WSL can start them" : "off: media opens in Linux apps",
             "set [interop] enabled=true in /etc/wsl.conf, then run wsl --shutdown in Windows");
    }
    int poppler = process_on_path("pdftoppm") && process_on_path("pdfinfo");
    line(t, poppler ? OK : WARN, "PDF pages", poppler ? "pdftoppm and pdfinfo" : "not found",
         install_hint(platform_is_macos() ? "poppler  (shows PDF pages inside tawk)" : "poppler-utils  (shows PDF pages inside tawk)"));

    const char *paste = NULL;
    if (platform_is_macos()) paste = process_on_path("pngpaste") ? "pngpaste" : NULL;
    else if (getenv("WAYLAND_DISPLAY") && process_on_path("wl-paste")) paste = "wl-paste";
    else if (getenv("DISPLAY") && process_on_path("xclip")) paste = "xclip";
    else if (platform_wsl_interop() && process_on_path("powershell.exe")) paste = "powershell.exe";
    line(t, paste ? OK : WARN, "clipboard pictures", paste ? paste : "no tool found",
         platform_is_macos() ? install_hint("pngpaste  (Alt+V pastes a picture)")
                             : install_hint("wl-clipboard xclip  (Alt+V pastes a picture)"));
}

static void check_screensaver(Tally *t, const DoctorInputs *in) {
    section("Screensaver");
    const Settings *s = in->settings;
    char program[256];
    program_of(s->screensaver_command, program, sizeof(program));
    if (!program[0]) { line(t, OK, "command", "none set", NULL); return; }
    int found = process_on_path(program);
    char buf[600];
    snprintf(buf, sizeof(buf), "%s%s", s->screensaver_command, found ? "" : " (not installed)");
    line(t, found ? OK : WARN, "command", buf, "install it, or pick another command under Settings, Screensaver");
}

/* One account's login folder: whether a number is linked there. */
static void account_line(Tally *t, const char *label, const char *auth_dir) {
    char detail[700];
    int linked = 0;
    DIR *d = opendir(auth_dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) if (e->d_name[0] != '.') linked = 1;
        closedir(d);
    }
    snprintf(detail, sizeof(detail), "%s (%s)", auth_dir, linked ? "linked" : "not linked yet");
    line(t, OK, label, detail, NULL);
}

/* The accounts, as their login folders show them. Labels and agent access are
 * kept in the database, which this check leaves alone: Settings, Account, Accounts shows those. */
static void check_accounts(Tally *t, const DoctorInputs *in) {
    section("Accounts");
    char path[600];
    path_join(path, sizeof(path), in->settings->data_dir, "auth");
    account_line(t, "account 1", path);
    char accounts[600];
    path_join(accounts, sizeof(accounts), in->settings->data_dir, "accounts");
    DIR *d = opendir(accounts);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int id = atoi(e->d_name);
        if (id <= 1) continue;
        char label[32], auth[900];
        snprintf(label, sizeof(label), "account %d", id);
        snprintf(auth, sizeof(auth), "%s/%s/auth", accounts, e->d_name);
        if (is_dir(auth)) account_line(t, label, auth);
    }
    closedir(d);
}

int doctor_run(const DoctorInputs *in) {
    Tally t = { 0, 0 };
    printf("%s %s setup check\n", APP_NAME, APP_VERSION);
    check_terminal(&t);
    check_files(&t, in);
    check_accounts(&t, in);
    check_backend(&t, in);
    check_media(&t, in);
    check_screensaver(&t, in);
    printf("\n");
    if (t.failures) printf("%d problem%s must be fixed before %s can run", t.failures, t.failures == 1 ? "" : "s", APP_NAME);
    else printf("Ready to run");
    if (t.warnings) printf("; %d optional item%s missing (see the hints above)", t.warnings, t.warnings == 1 ? "" : "s");
    printf(".\n");
    return t.failures ? 1 : 0;
}
