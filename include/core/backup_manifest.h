#ifndef APP_CORE_BACKUP_MANIFEST_H
#define APP_CORE_BACKUP_MANIFEST_H

#include <stdint.h>

/* 2 added the logins of further accounts. A backup of format 1 still restores: it holds the one account. */
#define BACKUP_FORMAT 2
#define BACKUP_FORMAT_OLDEST 1

/* What a backup holds, written into it as manifest.txt. */
typedef struct BackupManifest {
    int     format;              /* BACKUP_FORMAT when written by this version */
    char    app_version[32];
    int64_t created;             /* epoch seconds */
    int     has_database;
    int     database_encrypted;  /* the database inside is encrypted with the chats' passphrase */
    int     has_config;
    int     has_themes;
    int     has_media;
    int     has_login;
    int     has_accounts;        /* the logins of accounts after the first */
} BackupManifest;

#endif
