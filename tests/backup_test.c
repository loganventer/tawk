/* Encrypted backups: a backup holds the chats, settings, themes, media and
 * login, is unreadable without its passphrase, never replaces a file, and
 * restores everything while keeping what it replaced; a tampered archive
 * (a "../" member or a link) is refused before anything is extracted. */
#include "engines/backup_manifest_codec.h"
#include "engines/archive_path_policy.h"
#include "infrastructure/openssl_file_cipher.h"
#include "infrastructure/tar_archive.h"
#include "managers/backup_manager.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_file_snapshot.h"
#include "utilities/path_util.h"
#include "utilities/process_quiet.h"
#include "utilities/process_util.h"

#include <dirent.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static char root[64];
static char data_dir[200], db_path[260], config_dir[200], config_path[260], themes_dir[260], media_dir[200], auth_dir[260];

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static int file_has(const char *path, const char *text) {
    char buf[256] = "";
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, text) != NULL;
}

static int message_count(void) {
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) return -1;
    sqlite3_stmt *st = NULL;
    int n = -1;
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM messages", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    sqlite_database_close(db);
    return n;
}

/* How many entries in `dir` start with `prefix`. */
static int count_with(const char *dir, const char *prefix) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) n += strncmp(e->d_name, prefix, strlen(prefix)) == 0;
    closedir(d);
    return n;
}

static void make_data(void) {
    snprintf(data_dir, sizeof(data_dir), "%s/data", root);
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", data_dir);
    snprintf(config_dir, sizeof(config_dir), "%s/config", root);
    snprintf(config_path, sizeof(config_path), "%s/config.ini", config_dir);
    snprintf(themes_dir, sizeof(themes_dir), "%s/themes", config_dir);
    snprintf(media_dir, sizeof(media_dir), "%s/media", root);
    snprintf(auth_dir, sizeof(auth_dir), "%s/auth", data_dir);
    mkdir(data_dir, 0700);
    mkdir(config_dir, 0700);
    mkdir(themes_dir, 0700);
    mkdir(media_dir, 0700);
    mkdir(auth_dir, 0700);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (db) {
        sqlite3_exec(db, "INSERT INTO messages (id, chat_jid, sender_jid, text, ts) VALUES"
                         " ('A1', 'x@s.whatsapp.net', 'x@s.whatsapp.net', 'Hello', 1), ('A2', 'x@s.whatsapp.net', 'x@s.whatsapp.net', 'Again', 2);",
                     NULL, NULL, NULL);
        sqlite_database_close(db);
    }
    write_file(config_path, "[appearance]\ntheme = dracula\n");
    char path[400];
    snprintf(path, sizeof(path), "%s/mine.json", themes_dir);
    write_file(path, "{\"id\":\"mine\"}");
    snprintf(path, sizeof(path), "%s/photo.jpg", media_dir);
    write_file(path, "jpeg bytes");
    snprintf(path, sizeof(path), "%s/whatsmeow.db", auth_dir);
    write_file(path, "login");
}

static void test_round_trip(BackupManager *m, const BackupPaths *paths) {
    Passphrase right, wrong;
    passphrase_set(&right, "backup passphrase");
    passphrase_set(&wrong, "something else");
    char why[1300], backup[300];
    snprintf(backup, sizeof(backup), "%s/tawk-backup.enc", root);

    BackupRequest request = { *paths, backup, 1, 1 };
    CHECK(backup_manager_backup(m, &request, &right, why, sizeof(why)) == 0, "a backup is written");
    struct stat st;
    CHECK(stat(backup, &st) == 0 && (st.st_mode & 0777) == 0600, "only you can read it");
    CHECK(file_has(backup, "Salted__") && !file_has(backup, "dracula"), "and it is encrypted");
    CHECK(count_with(data_dir, ".backup-") == 0, "the working folder is removed");
    CHECK(backup_manager_backup(m, &request, &right, why, sizeof(why)) != 0, "an existing file is never replaced");

    /* Lose some data, change the rest. */
    unlink(db_path);
    char lost[400];
    snprintf(lost, sizeof(lost), "%s/2/auth/whatsmeow.db", paths->accounts_dir);
    unlink(lost);
    write_file(config_path, "[appearance]\ntheme = nord\n");
    RestoreReport report;
    CHECK(backup_manager_restore(m, paths, backup, &wrong, &report, why, sizeof(why)) != 0, "a wrong passphrase restores nothing");
    CHECK(file_has(config_path, "nord") && access(db_path, F_OK) != 0, "and changes nothing");

    CHECK(backup_manager_restore(m, paths, backup, &right, &report, why, sizeof(why)) == 0, "the backup restores");
    CHECK(message_count() == 2, "with every message");
    CHECK(file_has(config_path, "dracula"), "the settings");
    char path[400];
    snprintf(path, sizeof(path), "%s/mine.json", themes_dir);
    CHECK(access(path, F_OK) == 0, "your themes");
    snprintf(path, sizeof(path), "%s/photo.jpg", media_dir);
    CHECK(file_has(path, "jpeg"), "the media");
    snprintf(path, sizeof(path), "%s/whatsmeow.db", auth_dir);
    CHECK(file_has(path, "login"), "and the login");
    snprintf(path, sizeof(path), "%s/2/auth/whatsmeow.db", paths->accounts_dir);
    CHECK(file_has(path, "second login") && report.manifest.has_accounts && report.manifest.format == BACKUP_FORMAT,
          "and the logins of the other accounts");
    BackupManifest old;
    CHECK(backup_manifest_parse("format=1\nversion=0.6.4\ndatabase=1\nlogin=1\n", &old) == 0 && old.has_login && !old.has_accounts,
          "a backup from before accounts still reads, as the one account");
    CHECK(backup_manifest_parse("format=3\ndatabase=1\n", &old) != 0, "one from a newer tawk is refused");
    CHECK(report.manifest.has_media && report.manifest.has_login && report.moved_aside >= 3, "the report says what happened");
    snprintf(path, sizeof(path), "%s%s", config_path, report.aside_suffix);
    CHECK(file_has(path, "nord"), "what was replaced is kept aside");
    CHECK(stat(config_path, &st) == 0 && (st.st_mode & 0777) == 0600, "restored files are private");
    CHECK(count_with(data_dir, ".restore-") == 0, "the working folder is removed");
}

/* Packs `member` from `dir` into a tar, keeping its name as given, and encrypts it like a backup. */
static void make_bad_backup(const char *dir, const char *member, const char *out, const Passphrase *p) {
    char tarball[300];
    snprintf(tarball, sizeof(tarball), "%s/bad.tar.gz", root);
    unlink(tarball);
    char *argv[] = { "tar", "-czf", tarball, "-P", "-C", (char *)dir, (char *)member, NULL };
    process_run_quiet(argv, NULL, 0, -1);
    IFileCipher *cipher = openssl_file_cipher_create();
    unlink(out);
    cipher->encrypt(cipher, tarball, out, p);
    cipher->destroy(cipher);
}

static void test_tampered(BackupManager *m, const BackupPaths *paths) {
    Passphrase p;
    passphrase_set(&p, "tampered");
    char inner[300], outside[300], bad[300], why[1300];
    snprintf(inner, sizeof(inner), "%s/work/inner", root);
    snprintf(outside, sizeof(outside), "%s/work/evil", root);
    snprintf(bad, sizeof(bad), "%s/bad.enc", root);
    char work[300];
    snprintf(work, sizeof(work), "%s/work", root);
    mkdir(work, 0700);
    mkdir(inner, 0700);
    write_file(outside, "escaped");
    make_bad_backup(inner, "../evil", bad, &p);
    RestoreReport report;
    unlink(outside);
    CHECK(backup_manager_restore(m, paths, bad, &p, &report, why, sizeof(why)) != 0 && strstr(why, "../evil"),
          "a backup with a ../ member is refused");
    CHECK(access(outside, F_OK) != 0, "and nothing is written outside");

    char link_dir[300], link[320];
    snprintf(link_dir, sizeof(link_dir), "%s/work/links", root);
    mkdir(link_dir, 0700);
    snprintf(link, sizeof(link), "%s/themes", link_dir);
    if (symlink("/etc", link) != 0) perror("symlink");
    make_bad_backup(link_dir, "themes", bad, &p);
    CHECK(backup_manager_restore(m, paths, bad, &p, &report, why, sizeof(why)) != 0, "a backup holding a link is refused");
}

static void test_policy(void) {
    ArchiveEntry ok_db = { "tawk.db", '-' }, ok_media = { "media/abc.jpg", '-' }, ok_dir = { "themes/", 'd' };
    ArchiveEntry up = { "media/../../x", '-' }, absolute = { "/etc/passwd", '-' }, other = { "notes.txt", '-' }, link = { "auth", 'l' };
    CHECK(archive_path_allowed(&ok_db) && archive_path_allowed(&ok_media) && archive_path_allowed(&ok_dir), "backup members are allowed");
    CHECK(!archive_path_allowed(&up) && !archive_path_allowed(&absolute) && !archive_path_allowed(&other) && !archive_path_allowed(&link),
          "climbing, absolute, unknown and link members are not");
}

int main(void) {
    test_policy();
    if (!process_on_path("tar") || !process_on_path("openssl")) {
        printf("backup_test: tar or openssl missing, round trip skipped\n");
        return failures != 0;
    }
    snprintf(root, sizeof(root), "/tmp/tawk-backup-XXXXXX");
    if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
    make_data();
    IDatabaseSnapshot *snapshot = sqlite_file_snapshot_create();
    IArchive *archive = tar_archive_create();
    IFileCipher *cipher = openssl_file_cipher_create();
    BackupManagerDeps deps = { snapshot, archive, cipher, 0 };
    BackupManager *m = backup_manager_create(&deps);
    char accounts_dir[300], second[340], second_login[380];
    snprintf(accounts_dir, sizeof(accounts_dir), "%s/accounts", data_dir);
    snprintf(second, sizeof(second), "%s/2/auth", accounts_dir);
    snprintf(second_login, sizeof(second_login), "%s/whatsmeow.db", second);
    path_mkdir_p(second, 0700);
    write_file(second_login, "second login");
    BackupPaths paths = { data_dir, db_path, config_path, themes_dir, media_dir, auth_dir, accounts_dir };
    test_round_trip(m, &paths);
    test_tampered(m, &paths);
    backup_manager_destroy(m);
    cipher->destroy(cipher);
    archive->destroy(archive);
    snapshot->destroy(snapshot);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", root);
    if (failures == 0) printf("ok: backups are encrypted, restore everything, keep what they replace and refuse tampering\n");
    return failures != 0;
}
