#ifndef APP_ENGINES_CLIENT_VERSION_H
#define APP_ENGINES_CLIENT_VERSION_H

/* Whether a version such as "0.10.1" is at least major.minor. Text that is
 * not a version is not. */
int client_version_at_least(const char *version, int major, int minor);
/* Which of two versions is newer: below 0 when `a` is older, above 0 when it
 * is newer, 0 when they are the same or neither is a version. */
int client_version_compare(const char *a, const char *b);

#endif
