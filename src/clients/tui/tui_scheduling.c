/* Messages to send later: /later and /scheduled, carried out through the
 * scheduling manager, with the loop handing due messages to the messaging
 * manager. */
#include "tui_app_state.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LATE_SECONDS 120   /* sent this long after it was due: tell the user */

static const Settings *settings(TuiApp *app) { return settings_manager_current(app->deps.settings); }

void tui_app_schedule_later(TuiApp *app, const char *args) {
    const char *jid = messaging_manager_open_jid(app->deps.messaging);
    if (!jid[0]) { tui_app_toast(app, "Open a chat first", 1); return; }
    int64_t due = 0;
    if (scheduling_manager_schedule_line(app->deps.scheduling, jid, args, (int64_t)time(NULL), &due, NULL, 0) != 0) {
        tui_app_toast(app, scheduling_manager_error(app->deps.scheduling), 1);
        return;
    }
    char when[48], msg[96];
    clock_format_upcoming(due, settings(app)->use_24h_clock, when, sizeof(when));
    snprintf(msg, sizeof(msg), "\xF0\x9F\x95\x93 It goes %s%s", strncmp(when, "Today", 5) == 0 ? "at " : "", when);
    tui_app_toast(app, msg, 0);
    app->dirty = 1;
}

void tui_app_open_scheduled(TuiApp *app) {
    if (tui_app_show_login(app)) return;
    scheduled_list_dialog_open(&app->scheduled_list);
    app->dirty = 1;
}

static void reschedule(TuiApp *app, const char *id) {
    char *when = scheduled_list_dialog_when(&app->scheduled_list);
    int64_t due = 0;
    int rc = scheduling_manager_reschedule_text(app->deps.scheduling, id, when ? when : "", (int64_t)time(NULL), &due);
    free(when);
    if (rc != 0) { scheduled_list_dialog_error(&app->scheduled_list, scheduling_manager_error(app->deps.scheduling)); return; }
    scheduled_list_dialog_rescheduled(&app->scheduled_list);
    char label[48], msg[96];
    clock_format_upcoming(due, settings(app)->use_24h_clock, label, sizeof(label));
    snprintf(msg, sizeof(msg), "\xF0\x9F\x95\x93 Moved to %s", label);
    tui_app_toast(app, msg, 0);
}

void tui_app_scheduled_request(TuiApp *app, ScheduledListRequest request) {
    const char *id = scheduled_list_dialog_selected(&app->scheduled_list);
    char chosen[64] = "";
    if (id) str_copy(chosen, sizeof(chosen), id);
    switch (request) {
        case SCHEDULED_REQUEST_SEND_NOW:
            scheduling_manager_send_now(app->deps.scheduling, chosen, (int64_t)time(NULL));
            tui_app_toast(app, messaging_manager_auth_state(app->deps.messaging) == AUTH_STATE_CONNECTED
                               ? "Sending it now" : "It goes as soon as WhatsApp is connected", 0);
            break;
        case SCHEDULED_REQUEST_CANCEL:
            if (scheduling_manager_cancel(app->deps.scheduling, chosen) == 0) tui_app_toast(app, "Scheduled message cancelled", 0);
            break;
        case SCHEDULED_REQUEST_RESCHEDULE:
            reschedule(app, chosen);
            break;
        case SCHEDULED_REQUEST_NONE:
            return;
        default:
            break;
    }
    app->dirty = 1;
}

static void chat_name(void *ctx, const char *jid, char *out, size_t size) {
    messaging_manager_display_name(((TuiApp *)ctx)->deps.messaging, jid, out, size);
}

void tui_app_scheduled_render(TuiApp *app, UiRect area) {
    ScheduledMessage *items = NULL;
    int count = 0;
    scheduling_manager_list(app->deps.scheduling, NULL, &items, &count);
    scheduled_list_dialog_render(&app->scheduled_list, area, items, count, chat_name, app, settings(app)->use_24h_clock);
    scheduled_message_array_free(items, count);
}

/* Sends what is due for one account through that account's managers.
 * Returns how many went; *late gets how many of them were overdue. -1 when
 * some were due and none could be sent, 0 when nothing was due. */
int tui_scheduling_send_due(SchedulingManager *scheduling, MessagingManager *messaging, int *late) {
    *late = 0;
    if (!scheduling || messaging_manager_auth_state(messaging) != AUTH_STATE_CONNECTED) return 0;
    int64_t now = (int64_t)time(NULL);
    ScheduledMessage *due = NULL;
    int count = 0;
    if (scheduling_manager_take_due(scheduling, now, &due, &count) != 0 || count == 0) {
        scheduled_message_array_free(due, count);
        return 0;
    }
    int sent = 0;
    for (int i = 0; i < count; i++) {
        MentionList mentions;
        mention_list_parse(&mentions, due[i].mentions);
        OutgoingText out = { due[i].text, NULL, mentions.count ? &mentions : NULL, 0, 0, 0 };
        if (messaging_manager_send_text_to(messaging, due[i].chat_jid, &out) == 0) {
            scheduling_manager_mark_sent(scheduling, due[i].id);
            messaging_manager_note_scheduled_sent(messaging, due[i].id, due[i].chat_jid);
            sent++;
            if (now - due[i].due_at > LATE_SECONDS) (*late)++;
        } else {
            scheduling_manager_mark_failed(scheduling, due[i].id);
        }
    }
    scheduled_message_array_free(due, count);
    return sent ? sent : -1;
}

/* Says what tui_scheduling_send_due did. */
void tui_app_scheduling_report(TuiApp *app, int sent, int late) {
    if (sent == 0) return;
    char msg[96];
    if (late) snprintf(msg, sizeof(msg), "\xF0\x9F\x95\x93 Sent %d scheduled message%s late (tawk was closed or offline)", late, late == 1 ? "" : "s");
    else if (sent == 1) snprintf(msg, sizeof(msg), "\xF0\x9F\x95\x93 Sent a scheduled message");
    else if (sent > 0) snprintf(msg, sizeof(msg), "\xF0\x9F\x95\x93 Sent %d scheduled messages", sent);
    else snprintf(msg, sizeof(msg), "A scheduled message could not be sent");
    tui_app_toast(app, msg, sent < 0);
    app->dirty = 1;
}

/* Due messages go out while connected, each as an ordinary message (with
 * its own id, pending until WhatsApp takes it). Ones that were due while
 * tawk was closed go as soon as it is connected again, with a note. */
void tui_app_scheduling_tick(TuiApp *app) {
    if (!app->deps.scheduling) return;
    if (scheduling_manager_take_changed(app->deps.scheduling)) app->dirty = 1;
    int late = 0;
    int sent = tui_scheduling_send_due(app->deps.scheduling, app->deps.messaging, &late);
    tui_app_scheduling_report(app, sent, late);
}
