#include "engines/client_version.h"

#include <stdio.h>

int client_version_compare(const char *a, const char *b) {
    int x[3] = { -1, -1, -1 }, y[3] = { -1, -1, -1 };
    if (a) sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
    if (b) sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

int client_version_at_least(const char *version, int major, int minor) {
    int have_major = 0, have_minor = 0;
    if (!version || sscanf(version, "%d.%d", &have_major, &have_minor) != 2) return 0;
    return have_major > major || (have_major == major && have_minor >= minor);
}
