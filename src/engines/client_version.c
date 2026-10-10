#include "engines/client_version.h"

#include <stdio.h>

int client_version_at_least(const char *version, int major, int minor) {
    int have_major = 0, have_minor = 0;
    if (!version || sscanf(version, "%d.%d", &have_major, &have_minor) != 2) return 0;
    return have_major > major || (have_major == major && have_minor >= minor);
}
