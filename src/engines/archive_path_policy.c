#include "engines/archive_path_policy.h"

#include <string.h>

static const char *const FILES[] = { "manifest.txt", "tawk.db", "tawk.db-wal", "config.ini" };
static const char *const FOLDERS[] = { "themes", "media", "auth", "accounts" };

/* True when a path component is "..". */
static int climbs(const char *name) {
    for (const char *p = name; *p;) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.') return 1;
        p = slash ? slash + 1 : p + len;
    }
    return 0;
}

int archive_path_allowed(const ArchiveEntry *e) {
    const char *name = e->name;
    if (e->type != '-' && e->type != 'd') return 0;           /* links and devices */
    if (!name[0] || name[0] == '/' || climbs(name)) return 0;
    if (!strncmp(name, "./", 2)) name += 2;
    for (size_t i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        if (e->type == '-' && !strcmp(name, FILES[i])) return 1;
    }
    for (size_t i = 0; i < sizeof(FOLDERS) / sizeof(FOLDERS[0]); i++) {
        size_t n = strlen(FOLDERS[i]);
        if (!strncmp(name, FOLDERS[i], n) && (name[n] == '\0' || name[n] == '/')) return 1;
    }
    return 0;
}
