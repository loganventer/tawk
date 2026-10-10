#include "engines/notification_policy.h"
#include "engines/quiet_hours_policy.h"

int notification_policy_should_notify(const Settings *s, const Chat *chat,
                                      const Message *msg, const NotificationMoment *at) {
    if (!at->live || msg->from_me) return 0;
    if (!s->notifications || s->do_not_disturb) return 0;
    /* Being mentioned gets through a muted chat, silenced groups and quiet hours, as on the phone. */
    int mentioned = msg->mentions_me && s->mention_notifications;
    if (quiet_hours_policy_quiet(s->quiet_hours, s->quiet_hours_weekend, at->weekday, at->minute_of_day) && !mentioned) return 0;
    if (chat && chat->is_muted && !mentioned) return 0;
    if (chat && chat->is_group && !s->group_notifications && !mentioned) return 0;
    /* A chat set to mentions only alerts for a mention and nothing else, whatever the setting above says. */
    if (at->level == CHAT_ALERT_MENTIONS && !msg->mentions_me) return 0;
    if (at->chat_is_open) return 0;
    return 1;
}
