#include "engines/backup_manifest_codec.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int backup_manifest_format(const BackupManifest *m, char *out, size_t size) {
    int n = snprintf(out, size,
                     "# tawk backup\n"
                     "format=%d\nversion=%s\ncreated=%lld\n"
                     "database=%d\ndatabase_encrypted=%d\nconfig=%d\nthemes=%d\nmedia=%d\nlogin=%d\naccounts=%d\n",
                     m->format, m->app_version, (long long)m->created, m->has_database, m->database_encrypted,
                     m->has_config, m->has_themes, m->has_media, m->has_login, m->has_accounts);
    return n > 0 && (size_t)n < size ? 0 : -1;
}

static void set_field(BackupManifest *m, const char *key, const char *value) {
    int v = atoi(value);
    if (!strcmp(key, "format")) m->format = v;
    else if (!strcmp(key, "version")) str_copy(m->app_version, sizeof(m->app_version), value);
    else if (!strcmp(key, "created")) m->created = strtoll(value, NULL, 10);
    else if (!strcmp(key, "database")) m->has_database = v != 0;
    else if (!strcmp(key, "database_encrypted")) m->database_encrypted = v != 0;
    else if (!strcmp(key, "config")) m->has_config = v != 0;
    else if (!strcmp(key, "themes")) m->has_themes = v != 0;
    else if (!strcmp(key, "media")) m->has_media = v != 0;
    else if (!strcmp(key, "login")) m->has_login = v != 0;
    else if (!strcmp(key, "accounts")) m->has_accounts = v != 0;
}

int backup_manifest_parse(const char *text, BackupManifest *out) {
    memset(out, 0, sizeof(*out));
    for (const char *line = text; line && *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char buf[256];
        if (len < sizeof(buf) && line[0] != '#') {
            memcpy(buf, line, len);
            buf[len] = '\0';
            char *eq = strchr(buf, '=');
            if (eq) { *eq = '\0'; set_field(out, buf, eq + 1); }
        }
        line = end ? end + 1 : NULL;
    }
    return out->format >= BACKUP_FORMAT_OLDEST && out->format <= BACKUP_FORMAT ? 0 : -1;
}
