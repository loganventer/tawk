#include "clients/cli/backup_command.h"
#include "clients/cli/cli_confirm.h"
#include "utilities/app_info.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

static void describe(const char *label, int included) {
    printf("  %-14s %s\n", label, included ? "included" : "left out");
}

int backup_command_run(BackupManager *mgr, IPassphrasePrompt *prompt, const BackupRequest *r) {
    if (!backup_manager_available(mgr)) {
        fprintf(stderr, "%s: backups are encrypted with openssl, which is not installed\n", APP_NAME);
        return 1;
    }
    printf("Backing up to %s:\n", r->output);
    describe("chats", 1);
    describe("settings", 1);
    describe("your themes", 1);
    describe("media", r->with_media);
    describe("WhatsApp login", r->with_login);
    if (r->with_login) {
        printf("\nWith the login, anyone who has this file and its passphrase can use your WhatsApp account\n"
               "as a linked device. Keep both safe.\n");
    }
    printf("\nThe backup is encrypted with a passphrase. You need it to restore; it cannot be recovered.\n");
    Passphrase passphrase;
    if (prompt->ask(prompt, "Passphrase for the backup: ", 1, &passphrase) != 0) {
        fprintf(stderr, "%s: no passphrase given; nothing was written\n", APP_NAME);
        return 1;
    }
    char why[512];
    int rc = backup_manager_backup(mgr, r, &passphrase, why, sizeof(why));
    passphrase_wipe(&passphrase);
    if (rc != 0) {
        fprintf(stderr, "%s: no backup was made: %s\n", APP_NAME, why);
        return 1;
    }
    struct stat st;
    double mb = stat(r->output, &st) == 0 ? (double)st.st_size / (1024.0 * 1024.0) : 0;
    printf("Backup written: %s (%.1f MB)\n", r->output, mb);
    return 0;
}

static void restored(const char *label, int present) {
    if (present) printf("  %s\n", label);
}

int restore_command_run(BackupManager *mgr, IPassphrasePrompt *prompt, const BackupPaths *paths, const char *input,
                        int assume_yes) {
    if (!backup_manager_available(mgr)) {
        fprintf(stderr, "%s: backups are decrypted with openssl, which is not installed\n", APP_NAME);
        return 1;
    }
    printf("Restoring %s replaces your current chats, settings and themes (and media and login, when the\n"
           "backup has them). What it replaces is kept beside it with a .before-restore suffix.\n", input);
    if (!cli_confirm("Restore now?", assume_yes)) {
        printf("Nothing was changed.\n");
        return 1;
    }
    Passphrase passphrase;
    if (prompt->ask(prompt, "Passphrase of the backup: ", 0, &passphrase) != 0) {
        fprintf(stderr, "%s: no passphrase given; nothing was changed\n", APP_NAME);
        return 1;
    }
    RestoreReport report;
    char why[1300];
    int rc = backup_manager_restore(mgr, paths, input, &passphrase, &report, why, sizeof(why));
    passphrase_wipe(&passphrase);
    if (rc != 0) {
        fprintf(stderr, "%s: %s\n", APP_NAME, why);
        return 1;
    }
    char when[64] = "";
    time_t created = (time_t)report.manifest.created;
    struct tm tm_created;
    if (created > 0 && localtime_r(&created, &tm_created)) strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tm_created);
    printf("Restored the backup made %s by %s %s:\n", when, APP_NAME, report.manifest.app_version);
    restored(report.manifest.database_encrypted ? "chats (encrypted: tawk will ask for their passphrase)" : "chats",
             report.manifest.has_database);
    restored("settings", report.manifest.has_config);
    restored("your themes", report.manifest.has_themes);
    restored("media", report.manifest.has_media);
    restored("WhatsApp login", report.manifest.has_login);
    if (report.unchecked) {
        printf("This backup was made by an older tawk and carries no check against changes, so tawk could not tell whether it was altered. "
               "Make a new backup to have one that can.\n");
    }
    if (report.moved_aside) {
        printf("Your previous data was kept with the suffix %s; delete it once you are happy.\n", report.aside_suffix);
    }
    if (!report.manifest.has_login) {
        printf("The backup has no WhatsApp login, so the current one was left as it is (link your phone again if there is none).\n");
    }
    return 0;
}
