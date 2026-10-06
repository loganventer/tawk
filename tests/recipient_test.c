/* Finding someone who has no chat yet: what counts as a phone number, how a
 * contact's name is matched, and what the automation rules keep out. */
#include "core/chat.h"
#include "core/contact.h"
#include "core/settings.h"
#include "engines/recipient_reference_parser.h"
#include "engines/recipient_resolver.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define KOOS   "27821111111@s.whatsapp.net"
#define KOBUS  "27822222222@s.whatsapp.net"
#define SANNIE "27823333333@s.whatsapp.net"
#define MOM    "27820000001@s.whatsapp.net"

static int parses(const char *ref, const char *want) {
    char jid[128] = "";
    return recipient_reference_parse(ref, jid, sizeof(jid)) == 0 && strcmp(jid, want) == 0;
}

static int refused(const char *ref) {
    char jid[128] = "";
    return recipient_reference_parse(ref, jid, sizeof(jid)) != 0;
}

static void test_numbers(void) {
    CHECK(parses("+27821111111", KOOS) && parses("27821111111", KOOS) && parses("+27 82 111 1111", KOOS) &&
          parses("+27 (82) 111-1111", KOOS) && parses("0027821111111", KOOS) && parses("27.82.111.1111", KOOS),
          "a number with its country code is that person, however it is spaced");
    CHECK(parses(KOOS, KOOS), "and so is a personal JID");
    CHECK(refused("0821111111"), "a number written the local way names no country");
    CHECK(refused("+0821111111") && refused("12345") && refused("+1234567890123456"), "too short, too long or starting with 0 is not a number");
    CHECK(refused("Koos") && refused("082 111 1111 ext 5") && refused("") && refused(NULL), "words are not a number");
    CHECK(refused("120363000000000001@g.us") && refused("status@broadcast") && refused("120363000000000002@newsletter") &&
          refused("12345678901234@lid") && refused("koos@s.whatsapp.net"), "a group, a broadcast, a channel or anything else is not a person");
    CHECK(recipient_reference_is_person(KOOS) && !recipient_reference_is_person("120363000000000001@g.us") &&
          !recipient_reference_is_person("@s.whatsapp.net") && !recipient_reference_is_person(NULL), "only a person's JID is a person");
    char small[8];
    CHECK(recipient_reference_parse("+27821111111", small, sizeof(small)) != 0, "a JID that does not fit is refused, not cut short");
}

static Contact contact(const char *jid, const char *name, const char *push) {
    Contact c;
    contact_init(&c, jid);
    str_copy(c.name, sizeof(c.name), name);
    str_copy(c.push_name, sizeof(c.push_name), push);
    return c;
}

static void test_people(void) {
    Settings s;
    settings_set_defaults(&s);
    Chat chats[1];
    chat_init(&chats[0], MOM);
    str_copy(chats[0].name, sizeof(chats[0].name), "Mom");
    chats[0].is_locked = 1;                                 /* a chat agents may not use */
    Contact people[4] = {
        contact(KOOS, "Koos van der Merwe", "K"),
        contact(KOBUS, "", "Kobus Koos"),
        contact(SANNIE, "Sannie", ""),
        contact(MOM, "Mom", ""),
    };
    Recipient who, options[4];
    int n = 0;

    CHECK(recipient_resolve(&s, chats, 1, people, 4, "+27 82 111 1111", &who, options, 4, &n) == CHAT_RESOLUTION_FOUND &&
          !strcmp(who.jid, KOOS) && !strcmp(who.name, "Koos van der Merwe"), "a number finds the person and names them from the address book");
    CHECK(recipient_resolve(&s, chats, 1, NULL, 0, "+27829999999", &who, options, 4, &n) == CHAT_RESOLUTION_FOUND &&
          !strcmp(who.jid, "27829999999@s.whatsapp.net") && !strcmp(who.name, "+27829999999"), "a number nobody saved is still a person, named by the number");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "sannie", &who, options, 4, &n) == CHAT_RESOLUTION_FOUND && !strcmp(who.jid, SANNIE),
          "a whole name finds its contact in any case");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "Kobus Koos", &who, options, 4, &n) == CHAT_RESOLUTION_FOUND && !strcmp(who.jid, KOBUS) &&
          !strcmp(who.name, "Kobus Koos"), "the name someone chose counts where the address book has none");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "van", &who, options, 4, &n) == CHAT_RESOLUTION_FOUND && !strcmp(who.jid, KOOS),
          "the start of a word in a name finds it");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "Koos", &who, options, 4, &n) == CHAT_RESOLUTION_AMBIGUOUS && n == 2 &&
          !strcmp(options[0].jid, KOOS) && !strcmp(options[1].jid, KOBUS), "a name two people share is ambiguous, and both are offered");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "erwe", &who, options, 4, &n) == CHAT_RESOLUTION_NOT_FOUND && n == 0,
          "the middle of a word finds nobody");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "Mom", &who, options, 4, &n) == CHAT_RESOLUTION_NOT_FOUND &&
          recipient_resolve(&s, chats, 1, people, 4, MOM, &who, options, 4, &n) == CHAT_RESOLUTION_NOT_FOUND,
          "someone whose chat agents may not use is not found by name or by number");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "0821111111", &who, options, 4, &n) == CHAT_RESOLUTION_NOT_FOUND,
          "a local number finds nobody");

    str_copy(s.automation_chats, sizeof(s.automation_chats), "Sannie, +27 82 222 2222");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "Sannie", &who, options, 4, &n) == CHAT_RESOLUTION_FOUND &&
          recipient_resolve(&s, chats, 1, people, 4, "+27822222222", &who, options, 4, &n) == CHAT_RESOLUTION_FOUND,
          "with a list of chats agents may use, those on it are found");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "+27821111111", &who, options, 4, &n) == CHAT_RESOLUTION_NOT_FOUND &&
          recipient_resolve(&s, chats, 1, people, 4, "van", &who, options, 4, &n) == CHAT_RESOLUTION_NOT_FOUND,
          "and anyone else is not");
    CHECK(recipient_resolve(&s, chats, 1, people, 4, "Koos", &who, options, 4, &n) == CHAT_RESOLUTION_FOUND && !strcmp(who.jid, KOBUS),
          "so a shared name is no longer ambiguous when only one of them is allowed");
}

int main(void) {
    test_numbers();
    test_people();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: a number or a contact's name finds someone with no chat yet, and the automation rules still decide\n");
    return 0;
}
