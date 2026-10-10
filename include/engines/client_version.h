#ifndef APP_ENGINES_CLIENT_VERSION_H
#define APP_ENGINES_CLIENT_VERSION_H

/* Whether a version such as "0.10.1" is at least major.minor. Text that is
 * not a version is not. */
int client_version_at_least(const char *version, int major, int minor);

#endif
