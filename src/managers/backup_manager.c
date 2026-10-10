#include "managers/backup_manager.h"
#include "engines/archive_path_policy.h"
#include "engines/backup_manifest_codec.h"
#include "utilities/app_info.h"
#include "utilities/file_copy.h"
#include "utilities/log.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"
#include "utilities/tree_copy.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_MEMBERS 30000       /* files and folders in one backup, media included */
#define PATH_SIZE   1200

struct BackupManager {
    BackupManagerDeps deps;
};

BackupManager *backup_manager_create(const BackupManagerDeps *deps) {
    BackupManager *m = calloc(1, sizeof(*m));
    if (m) m->deps = *deps;
    return m;
}

void backup_manager_destroy(BackupManager *m) { free(m); }

int backup_manager_available(BackupManager *m) { return m->deps.cipher->available(m->deps.cipher); }

static int refuse(char *why, unsigned long size, const char *text) {
    str_copy(why, size, text);
    return -1;
}

static int is_dir(const char *p)  { struct stat st; return lstat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int is_file(const char *p) { struct stat st; return lstat(p, &st) == 0 && S_ISREG(st.st_mode); }

/* A private folder for the work, beside the data so hard links and renames stay on one filesystem. */
static int make_staging(const char *data_dir, const char *kind, char *out, size_t size) {
    snprintf(out, size, "%s/.%s-XXXXXX", data_dir, kind);
    return mkdtemp(out) ? 0 : -1;
}

/* A path too long to hold is left empty, so it can never name the wrong file. */
static void join(char *out, size_t size, const char *dir, const char *name) {
    int n = snprintf(out, size, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= size) out[0] = '\0';
}

static void with_suffix(char *out, size_t size, const char *path, const char *suffix) {
    int n = snprintf(out, size, "%s%s", path, suffix);
    if (n < 0 || (size_t)n >= size) out[0] = '\0';
}

/* ---- backup --------------------------------------------------------------- */

/* Copies what the backup holds into `content` and fills in the manifest; returns the entry names. */
static int gather(BackupManager *m, const BackupRequest *r, const char *content, BackupManifest *manifest,
                  const char **names, int *count) {
    const BackupPaths *p = &r->paths;
    char dest[PATH_SIZE];
    *count = 0;
    if (!is_file(p->db_path) || m->deps.snapshot->take(m->deps.snapshot, p->db_path, content) != 0) return -1;
    manifest->has_database = 1;
    manifest->database_encrypted = m->deps.database_encrypted;
    names[(*count)++] = "tawk.db";
    join(dest, sizeof(dest), content, "tawk.db-wal");
    if (is_file(dest)) names[(*count)++] = "tawk.db-wal";
    join(dest, sizeof(dest), content, "config.ini");
    if (is_file(p->config_path)) {
        if (file_copy(p->config_path, dest, 0600) != 0) return -1;
        manifest->has_config = 1;
        names[(*count)++] = "config.ini";
    }
    join(dest, sizeof(dest), content, "themes");
    if (is_dir(p->themes_dir)) {
        if (tree_copy(p->themes_dir, dest) != 0) return -1;
        manifest->has_themes = 1;
        names[(*count)++] = "themes";
    }
    join(dest, sizeof(dest), content, "media");
    if (r->with_media && is_dir(p->media_dir)) {
        if (tree_copy(p->media_dir, dest) != 0) return -1;
        manifest->has_media = 1;
        names[(*count)++] = "media";
    }
    join(dest, sizeof(dest), content, "auth");
    if (r->with_login && is_dir(p->auth_dir)) {
        if (tree_copy(p->auth_dir, dest) != 0) return -1;
        manifest->has_login = 1;
        names[(*count)++] = "auth";
    }
    join(dest, sizeof(dest), content, "accounts");
    if (r->with_login && p->accounts_dir && is_dir(p->accounts_dir)) {
        if (tree_copy(p->accounts_dir, dest) != 0) return -1;
        manifest->has_accounts = 1;
        names[(*count)++] = "accounts";
    }
    char text[512];
    join(dest, sizeof(dest), content, "manifest.txt");
    int fd = open(dest, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0 || backup_manifest_format(manifest, text, sizeof(text)) != 0 ||
        write(fd, text, strlen(text)) != (ssize_t)strlen(text)) {
        if (fd >= 0) close(fd);
        return -1;
    }
    close(fd);
    names[(*count)++] = "manifest.txt";
    return 0;
}

int backup_manager_backup(BackupManager *m, const BackupRequest *r, const Passphrase *passphrase, char *why, unsigned long size) {
    if (!backup_manager_available(m)) return refuse(why, size, "openssl is not installed, so the backup cannot be encrypted");
    if (!is_file(r->paths.db_path)) return refuse(why, size, "there is no database to back up yet");
    /* Claim the output first: never replace or write through anything already there. */
    int out = open(r->output, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (out < 0) return refuse(why, size, errno == EEXIST ? "that file already exists" : "that file cannot be created");
    close(out);

    char staging[PATH_SIZE], content[PATH_SIZE], archive[PATH_SIZE];
    if (make_staging(r->paths.data_dir, "backup", staging, sizeof(staging)) != 0) {
        unlink(r->output);
        return refuse(why, size, "cannot make a working folder in the data folder");
    }
    join(content, sizeof(content), staging, "content");
    join(archive, sizeof(archive), staging, "backup.tar.gz");
    BackupManifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    manifest.format = BACKUP_FORMAT;
    str_copy(manifest.app_version, sizeof(manifest.app_version), APP_VERSION);
    manifest.created = (int64_t)time(NULL);

    const char *names[8];
    int count = 0, rc = -1;
    if (mkdir(content, 0700) != 0 || gather(m, r, content, &manifest, names, &count) != 0) {
        refuse(why, size, "could not copy your data (is the disk full?)");
    } else if (m->deps.archive->create(m->deps.archive, content, names, count, archive) != 0) {
        refuse(why, size, "tar could not pack the backup");
    } else if (m->deps.cipher->encrypt(m->deps.cipher, archive, r->output, passphrase) != 0) {
        refuse(why, size, "openssl could not encrypt the backup");
    } else {
        chmod(r->output, 0600);
        rc = 0;
        LOG_INFO("backup written (media %d, login %d)", manifest.has_media, manifest.has_login);
    }
    tree_remove(staging);
    if (rc != 0) unlink(r->output);
    return rc;
}

/* ---- restore -------------------------------------------------------------- */

static int read_small_file(const char *path, char *out, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    ssize_t n = read(fd, out, size - 1);
    close(fd);
    if (n < 0) return -1;
    out[n] = '\0';
    return 0;
}

/* Every member must be one a backup holds; nothing is extracted otherwise. */
static int check_members(BackupManager *m, const char *archive, char *why, unsigned long size) {
    ArchiveEntry *entries = calloc(MAX_MEMBERS, sizeof(*entries));
    if (!entries) return refuse(why, size, "out of memory");
    int n = m->deps.archive->list(m->deps.archive, archive, entries, MAX_MEMBERS);
    int rc = n > 0 ? 0 : refuse(why, size, "the backup cannot be read (damaged, or not a tawk backup)");
    for (int i = 0; rc == 0 && i < n; i++) {
        if (!archive_path_allowed(&entries[i])) {
            char text[1200];
            snprintf(text, sizeof(text), "the backup holds something tawk never writes (%s); nothing was restored", entries[i].name);
            rc = refuse(why, size, text);
        }
    }
    free(entries);
    return rc;
}

/* Moves `target` aside (when it exists) and puts `source` in its place. */
static int put_in_place(const char *source, const char *target, const char *suffix, int *moved) {
    if (access(source, F_OK) != 0) return 0;
    char aside[PATH_SIZE], parent[PATH_SIZE];
    str_copy(parent, sizeof(parent), target);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) { *slash = '\0'; path_mkdir_p(parent, 0700); }
    struct stat st;
    if (lstat(target, &st) == 0) {
        with_suffix(aside, sizeof(aside), target, suffix);
        if (rename(target, aside) != 0) return -1;
        (*moved)++;
    }
    if (rename(source, target) == 0) { tree_make_private(target); return 0; }
    if (errno != EXDEV) return -1;
    int rc = is_dir(source) ? tree_copy(source, target) : file_copy(source, target, 0600);
    if (rc == 0) tree_make_private(target);
    return rc;
}

static int put_back(const BackupPaths *p, const char *content, const BackupManifest *mf, RestoreReport *report) {
    char src[PATH_SIZE], side[PATH_SIZE], wal[PATH_SIZE];
    const char *suffix = report->aside_suffix;
    int rc = 0;
    if (mf->has_database) {
        /* The current database's log and shared memory belong to it: they step aside with it. */
        const char *extras[] = { "-wal", "-shm" };
        for (int i = 0; i < 2; i++) {
            with_suffix(side, sizeof(side), p->db_path, extras[i]);
            char aside[PATH_SIZE];
            with_suffix(aside, sizeof(aside), side, suffix);
            if (access(side, F_OK) == 0 && rename(side, aside) == 0) report->moved_aside++;
        }
        join(src, sizeof(src), content, "tawk.db");
        rc |= put_in_place(src, p->db_path, suffix, &report->moved_aside);
        join(src, sizeof(src), content, "tawk.db-wal");
        with_suffix(wal, sizeof(wal), p->db_path, "-wal");
        rc |= put_in_place(src, wal, suffix, &report->moved_aside);
    }
    if (mf->has_config) { join(src, sizeof(src), content, "config.ini"); rc |= put_in_place(src, p->config_path, suffix, &report->moved_aside); }
    if (mf->has_themes) { join(src, sizeof(src), content, "themes"); rc |= put_in_place(src, p->themes_dir, suffix, &report->moved_aside); }
    if (mf->has_media)  { join(src, sizeof(src), content, "media"); rc |= put_in_place(src, p->media_dir, suffix, &report->moved_aside); }
    if (mf->has_login)  { join(src, sizeof(src), content, "auth"); rc |= put_in_place(src, p->auth_dir, suffix, &report->moved_aside); }
    if (mf->has_accounts && p->accounts_dir) {
        join(src, sizeof(src), content, "accounts");
        rc |= put_in_place(src, p->accounts_dir, suffix, &report->moved_aside);
    }
    return rc;
}

int backup_manager_restore(BackupManager *m, const BackupPaths *p, const char *input, const Passphrase *passphrase,
                           RestoreReport *report, char *why, unsigned long size) {
    memset(report, 0, sizeof(*report));
    if (!backup_manager_available(m)) return refuse(why, size, "openssl is not installed, so the backup cannot be decrypted");
    if (!is_file(input)) return refuse(why, size, "there is no such backup file");
    char staging[PATH_SIZE], content[PATH_SIZE], archive[PATH_SIZE], manifest_path[PATH_SIZE];
    path_mkdir_p(p->data_dir, 0700);
    if (make_staging(p->data_dir, "restore", staging, sizeof(staging)) != 0) {
        return refuse(why, size, "cannot make a working folder in the data folder");
    }
    join(content, sizeof(content), staging, "content");
    join(archive, sizeof(archive), staging, "backup.tar.gz");
    join(manifest_path, sizeof(manifest_path), content, "manifest.txt");

    int rc = -1;
    char text[1024];
    int checked = m->deps.cipher->checked && m->deps.cipher->checked(m->deps.cipher, input);
    if (m->deps.cipher->decrypt(m->deps.cipher, input, archive, passphrase) != 0) {
        refuse(why, size, "the passphrase is wrong, or the backup was changed or damaged, or this is not a tawk backup");
    } else if (check_members(m, archive, why, size) != 0) {
        /* why is set */
    } else if (mkdir(content, 0700) != 0 || m->deps.archive->extract(m->deps.archive, archive, content) != 0) {
        refuse(why, size, "tar could not unpack the backup");
    } else if (read_small_file(manifest_path, text, sizeof(text)) != 0 || backup_manifest_parse(text, &report->manifest) != 0) {
        refuse(why, size, "the backup was made by a version of tawk this one cannot read");
    } else if (report->manifest.format >= BACKUP_FORMAT_CHECKED && !checked) {
        /* It says it was written with the check, and the check is not on it: someone took it off. */
        refuse(why, size, "the check that guards this backup against changes was removed from it; nothing was restored");
    } else {
        report->unchecked = !checked;
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        strftime(report->aside_suffix, sizeof(report->aside_suffix), ".before-restore-%Y%m%d-%H%M%S", &tm_now);
        if (put_back(p, content, &report->manifest, report) != 0) {
            refuse(why, size, "some of the backup could not be put in place; your previous data was kept with the suffix shown");
        } else {
            rc = 0;
            LOG_INFO("backup restored (%d items moved aside)", report->moved_aside);
        }
    }
    tree_remove(staging);
    return rc;
}
