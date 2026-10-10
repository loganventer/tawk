#ifndef APP_UTILITIES_APP_INFO_H
#define APP_UTILITIES_APP_INFO_H

/* The application name drives the binary name, the config directory
 * (~/.config/APP_NAME), the data directory (~/.local/share/APP_NAME) and the
 * terminal title. Override at build time with: make APP=<name> */
#ifndef APP_NAME
#define APP_NAME "tawk"
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.14.5"
#endif

/* The git commit of this build ("" when built outside a checkout). */
#ifndef APP_COMMIT
#define APP_COMMIT ""
#endif

#define APP_AUTHOR       "Logan Venter"
#define APP_AUTHOR_EMAIL "logan.venter@outlook.com"
#define APP_HOMEPAGE     "https://github.com/loganventer/tawk"

#endif
