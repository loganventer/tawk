#ifndef APP_INFRASTRUCTURE_DESKTOP_NOTIFIER_H
#define APP_INFRASTRUCTURE_DESKTOP_NOTIFIER_H

#include "contracts/i_notifier.h"
#include "core/settings.h"

/* Shows a banner through the desktop's own program: terminal-notifier or
 * osascript on macOS, notify-send elsewhere. The program is one of those
 * fixed names, found on PATH and run with an argument list, never through a
 * shell. Does nothing unless system_notifications is "desktop" or "both",
 * or when none of the programs is there. */
INotifier *desktop_notifier_create(const Settings *settings);
/* The program it would use, or "" when there is none: for --doctor. */
const char *desktop_notifier_program(void);

#endif
