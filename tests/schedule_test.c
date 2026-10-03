/* Scheduled messages: reading times such as "18:00", "+30m" and "fri
 * 17:30", keeping messages until they are due (across a restart), and
 * moving, cancelling and marking them. */
#include "core/event.h"
#include "engines/schedule_time_parser.h"
#include "managers/scheduling_manager.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_scheduled_message_store.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

/* A local time on Thursday 1 October 2026. */
static int64_t local(int day, int hour, int minute) {
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_isdst = -1;
    return (int64_t)mktime(&tm);
}

static int parses(const char *text, int64_t now, int64_t want, const char *want_rest) {
    int64_t due = 0;
    const char *rest = NULL;
    if (schedule_time_parse(text, now, &due, &rest) != 0) return 0;
    return due == want && strcmp(rest, want_rest) == 0;
}

static int refused(const char *text, int64_t now) {
    int64_t due = 0;
    const char *rest = NULL;
    return schedule_time_parse(text, now, &due, &rest) != 0;
}

static void test_parser(void) {
    int64_t now = local(1, 10, 0);                     /* Thursday 10:00 */
    CHECK(parses("18:00 see you there", now, local(1, 18, 0), "see you there"), "18:00 is today at six");
    CHECK(parses("9:15 morning", now, local(2, 9, 15), "morning"), "a time already gone today is tomorrow");
    CHECK(parses("6:30pm dinner", now, local(1, 18, 30), "dinner") && parses("7pm x", now, local(1, 19, 0), "x"),
          "am and pm are understood");
    CHECK(parses("12am x", now, local(2, 0, 0), "x"), "12am is midnight");
    CHECK(parses("+30m hi", now, now + 1800, "hi") && parses("+1h30m hi", now, now + 5400, "hi") && parses("+2d", now, now + 2 * 86400, ""),
          "offsets from now");
    CHECK(parses("tomorrow hello", now, local(2, 9, 0), "hello") && parses("tomorrow 7:45 hi", now, local(2, 7, 45), "hi"),
          "tomorrow, at nine unless a time is given");
    CHECK(parses("today 18:00 x", now, local(1, 18, 0), "x") && refused("today 08:00 x", now), "today must still be ahead");
    CHECK(parses("fri 17:30 drinks", now, local(2, 17, 30), "drinks") && parses("Monday stand-up", now, local(5, 9, 0), "stand-up"),
          "weekdays, short or long, in any case");
    CHECK(parses("thu 11:00 x", now, local(1, 11, 0), "x") && parses("thu 08:00 x", now, local(8, 8, 0), "x"),
          "today's weekday is today while the time is ahead, else next week");
    CHECK(refused("hello there", now) && refused("25:00 x", now) && refused("18 x", now) && refused("+5x hi", now) &&
          refused("", now) && refused("+0m", now), "anything else is refused");

    int64_t seconds = 0;
    CHECK(schedule_time_parse_adjustment("+37s", &seconds) == 0 && seconds == 37 &&
          schedule_time_parse_adjustment(" -12s ", &seconds) == 0 && seconds == -12,
          "an adjustment in seconds reads either way");
    CHECK(schedule_time_parse_adjustment("12s", &seconds) != 0 && schedule_time_parse_adjustment("+5m", &seconds) != 0 &&
          schedule_time_parse_adjustment("+5s hi", &seconds) != 0 && schedule_time_parse_adjustment("+86401s", &seconds) != 0,
          "an adjustment must be a signed number of seconds and nothing more");
}

static ScheduledMessage *find(ScheduledMessage *items, int count, const char *id) {
    for (int i = 0; i < count; i++) if (strcmp(items[i].id, id) == 0) return &items[i];
    return NULL;
}

static void test_manager(const char *dir) {
    char path[600];
    snprintf(path, sizeof(path), "%s/tawk.db", dir);
    sqlite3 *db = sqlite_database_open(path, NULL);
    if (!db) { failures++; return; }
    IScheduledMessageStore *store = sqlite_scheduled_message_store_create(db, ACCOUNT_ID_FIRST);
    SchedulingManagerDeps deps = { store };
    SchedulingManager *m = scheduling_manager_create(&deps);
    int64_t now = local(1, 10, 0);

    char a[64], b[64], c[64];
    int64_t due = 0;
    CHECK(scheduling_manager_schedule_line(m, "27820000001@s.whatsapp.net", "nonsense text", now, &due, a, sizeof(a)) != 0 &&
          strstr(scheduling_manager_error(m), "18:00") != NULL, "a line without a time is refused with a hint");
    CHECK(scheduling_manager_schedule_line(m, "27820000001@s.whatsapp.net", "18:00", now, &due, a, sizeof(a)) != 0,
          "a time with nothing to send is refused");
    CHECK(scheduling_manager_schedule_line(m, "27820000001@s.whatsapp.net", "18:00 Dinner at ours", now, &due, a, sizeof(a)) == 0 &&
          due == local(1, 18, 0), "a message is kept for six o'clock");
    CHECK(scheduling_manager_take_changed(m) && !scheduling_manager_take_changed(m), "the change is reported once");
    CHECK(scheduling_manager_schedule(m, "1234567890@lid", "Happy birthday!", NULL, local(2, 8, 0), now, b, sizeof(b)) == 0,
          "and one for tomorrow morning");
    CHECK(scheduling_manager_schedule(m, "27820000001@s.whatsapp.net", "too late", NULL, now - 60, now, c, sizeof(c)) != 0,
          "a time in the past is refused");
    CHECK(scheduling_manager_schedule(m, "27820000002@s.whatsapp.net", "Stand-up in 5", NULL, now + 300, now, c, sizeof(c)) == 0,
          "and one in five minutes");

    CHECK(scheduling_manager_parse_when(m, "18:00 +37s", now, &due) == 0 && due == local(1, 18, 0) + 37 &&
          scheduling_manager_parse_when(m, "+5m -12s", now, &due) == 0 && due == now + 288,
          "a control request's time can be nudged by seconds");
    CHECK(scheduling_manager_parse_when(m, "+1m -60s", now, &due) == 0 && due == now + 1,
          "a nudge never moves a message into the past");
    CHECK(scheduling_manager_parse_when(m, "18:00 hello", now, &due) != 0, "anything else after the time is still refused");

    ScheduledMessage *items = NULL;
    int count = 0;
    scheduling_manager_list(m, NULL, &items, &count);
    CHECK(count == 3 && strcmp(items[0].id, c) == 0 && strcmp(items[1].id, a) == 0 && strcmp(items[2].id, b) == 0,
          "all of them, soonest first");
    scheduled_message_array_free(items, count);
    scheduling_manager_list(m, "27820000001@s.whatsapp.net", &items, &count);
    CHECK(count == 1 && strcmp(items[0].text, "Dinner at ours") == 0, "or one chat's");
    scheduled_message_array_free(items, count);

    Event alias;
    event_init(&alias, EVENT_JID_ALIAS);
    str_copy(alias.lid, sizeof(alias.lid), "1234567890@lid");
    str_copy(alias.jid, sizeof(alias.jid), "27820000003@s.whatsapp.net");
    scheduling_manager_observer(m)->on_event(scheduling_manager_observer(m), &alias);
    scheduling_manager_list(m, "27820000003@s.whatsapp.net", &items, &count);
    CHECK(count == 1 && strcmp(items[0].id, b) == 0, "a chat's hidden id turning out to be a number moves its messages");
    scheduled_message_array_free(items, count);

    scheduling_manager_take_due(m, now + 299, &items, &count);
    CHECK(count == 0, "nothing is due before its time");
    scheduled_message_array_free(items, count);
    scheduling_manager_take_due(m, now + 300, &items, &count);
    CHECK(count == 1 && strcmp(items[0].id, c) == 0, "then it is");
    scheduled_message_array_free(items, count);
    scheduling_manager_mark_sent(m, c);
    scheduling_manager_list(m, NULL, &items, &count);
    CHECK(count == 2 && !find(items, count, c), "a sent message is no longer waiting");
    scheduled_message_array_free(items, count);

    CHECK(scheduling_manager_reschedule_text(m, a, "+2h", now, &due) == 0 && due == now + 7200, "a message can be moved");
    CHECK(scheduling_manager_reschedule_text(m, a, "whenever", now, &due) != 0, "only to a time it understands");
    CHECK(scheduling_manager_send_now(m, b, now) == 0, "or sent now");
    scheduling_manager_take_due(m, now, &items, &count);
    CHECK(count == 1 && strcmp(items[0].id, b) == 0, "which makes it due at once");
    scheduled_message_array_free(items, count);
    scheduling_manager_mark_failed(m, b);
    CHECK(scheduling_manager_cancel(m, a) == 0 && scheduling_manager_cancel(m, a) != 0, "cancelling drops it, once");
    scheduling_manager_list(m, NULL, &items, &count);
    CHECK(count == 0, "failed and cancelled messages are no longer waiting");
    scheduled_message_array_free(items, count);

    /* Due while tawk was closed: still there after a restart, and due at once. */
    scheduling_manager_schedule(m, "27820000001@s.whatsapp.net", "See you soon", NULL, now + 60, now, a, sizeof(a));
    scheduling_manager_destroy(m);
    store->destroy(store);
    sqlite_database_close(db);
    db = sqlite_database_open(path, NULL);
    store = sqlite_scheduled_message_store_create(db, ACCOUNT_ID_FIRST);
    deps.store = store;
    m = scheduling_manager_create(&deps);
    scheduling_manager_take_due(m, now + 3600, &items, &count);
    CHECK(count == 1 && strcmp(items[0].id, a) == 0 && items[0].due_at == now + 60, "a message missed while closed goes on the next start");
    scheduled_message_array_free(items, count);

    scheduling_manager_destroy(m);
    store->destroy(store);
    sqlite_database_close(db);
}

int main(void) {
    char dir[] = "/tmp/tawk-schedule-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    test_parser();
    test_manager(dir);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures == 0) printf("ok: messages wait for 18:00, +30m or fri 17:30, survive a restart and can be moved or cancelled\n");
    return failures != 0;
}
