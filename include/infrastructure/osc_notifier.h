#ifndef APP_INFRASTRUCTURE_OSC_NOTIFIER_H
#define APP_INFRASTRUCTURE_OSC_NOTIFIER_H

#include "contracts/i_notifier.h"
#include "core/settings.h"

/* Asks the terminal itself for a banner, with the escape code it understands
 * (OSC 9, 99 or 777). Does nothing unless system_notifications is "terminal"
 * or "both". A terminal that knows none of them shows nothing. */
INotifier *osc_notifier_create(const Settings *settings);

#endif
