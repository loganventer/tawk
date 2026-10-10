/* Mentions: finding members while typing "@", turning "@Name" into what
 * WhatsApp sends, reading them back, and notifying when you are mentioned. */
#include "core/chat.h"
#include "core/event.h"
#include "core/settings.h"
#include "engines/mention_encoder.h"
#include "engines/mention_matcher.h"
#include "engines/notification_policy.h"
#include "utilities/str_util.h"
#include "core/notification.h"
#include "engines/banner_text.h"
#include "engines/quiet_hours_policy.h"
#include "resource_access/json_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static void test_matcher(void) {
    MentionCandidate members[4] = {
        { "1@s.whatsapp.net", "Jan de Wet" }, { "2@s.whatsapp.net", "Lindiwe" },
        { "3@s.whatsapp.net", "Marjan" },     { "4@s.whatsapp.net", "Pieter" },
    };
    MentionCandidate out[8];
    int n = mention_matcher_rank(members, 4, "jan", out, 8);
    CHECK(n == 2 && strcmp(out[0].name, "Jan de Wet") == 0 && strcmp(out[1].name, "Marjan") == 0,
          "a name starting with the typed text comes before one containing it");
    CHECK(mention_matcher_rank(members, 4, "WET", out, 8) == 1, "matching ignores case and looks at every word");
    CHECK(mention_matcher_rank(members, 4, "", out, 8) == 4, "a bare @ lists everyone");
    CHECK(mention_matcher_rank(members, 4, "zz", out, 8) == 0, "nothing fits");
}

static void test_encoder(void) {
    MentionPick picks[2] = { { "27820000001@s.whatsapp.net", "Jan" }, { "27820000002@s.whatsapp.net", "Jan de Wet" } };
    char out[256];
    MentionList list;
    CHECK(mention_encoder_encode("hi @Jan de Wet and @Jan!", picks, 2, out, sizeof(out), &list) == 0 &&
          strcmp(out, "hi @27820000002 and @27820000001!") == 0, "@Name becomes @number, the longest name first");
    CHECK(list.count == 2 && strcmp(list.items[0].user, "27820000002") == 0, "and both are listed");
    CHECK(mention_encoder_encode("no one here", picks, 2, out, sizeof(out), &list) == 0 && list.count == 0 &&
          strcmp(out, "no one here") == 0, "picks deleted from the text are left out");
    CHECK(mention_encoder_encode("@Jan", picks, 1, out, 4, &list) == -1, "too little room is refused");
}

static void test_protocol(void) {
    Event e;
    CHECK(json_protocol_decode("{\"evt\":\"message\",\"id\":\"M\",\"chat\":\"g@g.us\",\"sender\":\"a@s.whatsapp.net\",\"type\":\"text\","
                               "\"text\":\"hi @123\",\"mentions\":[{\"jid\":\"27820000001@s.whatsapp.net\",\"user\":\"123\"},"
                               "{\"jid\":\"bad\",\"user\":\"1\"},{\"jid\":\"x@lid\",\"user\":\"12a\"}],\"mentions_me\":true}", &e) == 0,
          "a message with mentions decodes");
    MentionList list;
    mention_list_parse(&list, e.message.mentions);
    CHECK(list.count == 1 && strcmp(list.items[0].user, "123") == 0 && e.message.mentions_me,
          "only well-formed mentions are kept, and mentioning you is flagged");
    event_dispose(&e);

    MentionList out;
    mention_list_init(&out);
    mention_list_add(&out, "27820000001@s.whatsapp.net", NULL);
    OutgoingText text = { "hi @27820000001", NULL, &out, 0, 0, 0 };
    char *json = json_protocol_encode_send("g@g.us", &text, "ID1");
    CHECK(json && strstr(json, "\"mentions\":[\"27820000001@s.whatsapp.net\"]"), "sending lists the mentioned JIDs");
    free(json);
}

static void test_policy(void) {
    Settings s;
    settings_set_defaults(&s);
    Chat chat;
    chat_init(&chat, "g@g.us");
    chat.is_group = 1;
    chat.is_muted = 1;
    Message msg;
    message_init(&msg);
    NotificationMoment noon = { 1, 0, CHAT_ALERT_ALL, 3, 12 * 60 };          /* a Wednesday at noon */
    CHECK(!notification_policy_should_notify(&s, &chat, &msg, &noon), "a muted group stays quiet");
    msg.mentions_me = 1;
    CHECK(notification_policy_should_notify(&s, &chat, &msg, &noon), "unless you are mentioned");
    s.do_not_disturb = 1;
    CHECK(!notification_policy_should_notify(&s, &chat, &msg, &noon), "do not disturb still wins");
    s.do_not_disturb = 0;
    s.mention_notifications = 0;
    CHECK(!notification_policy_should_notify(&s, &chat, &msg, &noon), "and the setting turns it off");

    /* Mentions only, per chat. */
    settings_set_defaults(&s);
    chat.is_muted = 0;
    msg.mentions_me = 0;
    NotificationMoment only = noon;
    only.level = CHAT_ALERT_MENTIONS;
    CHECK(notification_policy_should_notify(&s, &chat, &msg, &noon) && !notification_policy_should_notify(&s, &chat, &msg, &only),
          "a chat set to mentions only stays quiet for an ordinary message");
    msg.mentions_me = 1;
    CHECK(notification_policy_should_notify(&s, &chat, &msg, &only), "and alerts for one that mentions you");
    s.mention_notifications = 0;
    CHECK(notification_policy_should_notify(&s, &chat, &msg, &only), "even with mentions-always-notify off: the chat asked for mentions");

    /* Quiet hours. */
    settings_set_defaults(&s);
    msg.mentions_me = 0;
    str_copy(s.quiet_hours, sizeof(s.quiet_hours), "22:00-07:00");
    NotificationMoment late = { 1, 0, CHAT_ALERT_ALL, 3, 23 * 60 + 30 }, early = { 1, 0, CHAT_ALERT_ALL, 4, 6 * 60 + 59 },
                       morning = { 1, 0, CHAT_ALERT_ALL, 4, 7 * 60 };
    CHECK(!notification_policy_should_notify(&s, &chat, &msg, &late) && !notification_policy_should_notify(&s, &chat, &msg, &early),
          "nothing alerts you inside quiet hours, on either side of midnight");
    CHECK(notification_policy_should_notify(&s, &chat, &msg, &morning) && notification_policy_should_notify(&s, &chat, &msg, &noon),
          "and it does again from the minute they end");
    msg.mentions_me = 1;
    CHECK(notification_policy_should_notify(&s, &chat, &msg, &late), "a mention gets through quiet hours");
    msg.mentions_me = 0;
    str_copy(s.quiet_hours_weekend, sizeof(s.quiet_hours_weekend), "23:00-09:30");
    NotificationMoment saturday_8 = { 1, 0, CHAT_ALERT_ALL, 6, 8 * 60 }, saturday_2230 = { 1, 0, CHAT_ALERT_ALL, 6, 22 * 60 + 30 },
                       monday_8 = { 1, 0, CHAT_ALERT_ALL, 1, 8 * 60 };
    CHECK(!notification_policy_should_notify(&s, &chat, &msg, &saturday_8) && notification_policy_should_notify(&s, &chat, &msg, &saturday_2230) &&
          notification_policy_should_notify(&s, &chat, &msg, &monday_8), "the weekend has its own hours when you give it some");

    QuietHours h = quiet_hours_policy_parse("22-7");
    CHECK(h.set && h.from == 22 * 60 && h.until == 7 * 60, "hours may be written without minutes");
    h = quiet_hours_policy_parse(" 13:30 - 14:15 ");
    CHECK(h.set && quiet_hours_policy_covers(h, 13 * 60 + 30) && quiet_hours_policy_covers(h, 14 * 60 + 14) && !quiet_hours_policy_covers(h, 14 * 60 + 15),
          "a stretch inside one day covers from its first minute up to, not including, its last");
    CHECK(!quiet_hours_policy_parse("").set && !quiet_hours_policy_parse("22:00").set && !quiet_hours_policy_parse("25:00-07:00").set &&
          !quiet_hours_policy_parse("22:61-07:00").set && !quiet_hours_policy_parse("soon-later").set && !quiet_hours_policy_parse("08:00-08:00").set &&
          !quiet_hours_policy_parse("22:00-07:00 please").set && !quiet_hours_policy_parse(NULL).set,
          "anything that is not two times with a dash gives no quiet hours at all");

    /* The banner. */
    Notification n;
    memset(&n, 0, sizeof(n));
    str_copy(n.title, sizeof(n.title), "Mom\x1b]0;x\x07");
    str_copy(n.body, sizeof(n.body), "Dinner; at six\nbring bread");
    str_copy(n.account_label, sizeof(n.account_label), "Personal");
    char title[200], body[300];
    banner_text_title(&n, title, sizeof(title));
    banner_text_body(&n, body, sizeof(body));
    CHECK(!strchr(title, '\x1b') && !strchr(title, '\x07') && strstr(title, "Mom") == title && strstr(title, "(Personal)"),
          "a banner's title names the account and carries no escape of the sender's");
    CHECK(!strchr(body, ';') && !strchr(body, '\n') && strstr(body, "Dinner"), "its body cannot end the escape early or part its fields");
    n.body[0] = '\0';
    banner_text_body(&n, body, sizeof(body));
    CHECK(!strcmp(body, "New message"), "with previews off it says only that there is a message");
    CHECK(banner_text_osc("iTerm.app", NULL, "xterm-256color") == 9 && banner_text_osc(NULL, "3", "xterm-kitty") == 99 &&
          banner_text_osc("ghostty", "", "xterm-ghostty") == 9 && banner_text_osc(NULL, NULL, "foot") == 777 && banner_text_osc(NULL, NULL, NULL) == 777,
          "each terminal is asked in the way it understands");
}

int main(void) {
    test_matcher();
    test_encoder();
    test_protocol();
    test_policy();
    if (failures == 0) printf("ok: mentions are suggested, sent, read back and notify through a mute\n");
    return failures != 0;
}
