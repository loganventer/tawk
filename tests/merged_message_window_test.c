/* One conversation out of the same contact's chats in several accounts:
 * messages in the order they happened, each with the account it belongs
 * to, and a message that reached several accounts shown once. */
#include "clients/tui/merged_message_window.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define MAIN 1
#define WORK 2

static Message msg(const char *id, int64_t ts, char *text) {
    Message m;
    message_init(&m);
    str_copy(m.id, sizeof(m.id), id);
    m.timestamp = ts;
    m.text = text;                       /* borrowed by the window, owned by the test */
    return m;
}

static int is(const MergedMessageWindow *w, int i, const char *id, AccountId owner) {
    return i < w->count && strcmp(w->items[i].id, id) == 0 && w->owners[i] == owner;
}

int main(void) {
    char a[] = "from main", b[] = "from work", g[] = "to the group", g2[] = "to the group, as work has it";
    Message main_msgs[] = { msg("M1", 100, a), msg("G1", 150, g), msg("M2", 300, a) };
    Message work_msgs[] = { msg("W1", 120, b), msg("G1", 150, g2), msg("W2", 200, b), msg("W3", 300, b) };
    MessageSource sources[] = { { MAIN, main_msgs, 3 }, { WORK, work_msgs, 4 } };
    MergedMessageWindow w;
    merged_message_window_init(&w);

    merged_message_window_build(&w, sources, 2, MAIN);
    CHECK(w.count == 6, "a message that reached both accounts is there once");
    CHECK(is(&w, 0, "M1", MAIN) && is(&w, 1, "W1", WORK) && is(&w, 2, "G1", MAIN) && is(&w, 3, "W2", WORK),
          "messages come in the order they happened, each with its account");
    CHECK(is(&w, 4, "M2", MAIN) && is(&w, 5, "W3", WORK), "two at the same moment keep the order of their accounts");
    CHECK(w.count > 2 && w.items[2].text == g, "the shared message is the copy of the account in view");

    merged_message_window_build(&w, sources, 2, WORK);
    CHECK(w.count == 6 && is(&w, 2, "G1", WORK) && w.items[2].text == g2, "with the other account in view it is that account's copy");

    MessageSource one[] = { { WORK, work_msgs, 4 } };
    merged_message_window_build(&w, one, 1, WORK);
    CHECK(w.count == 4 && is(&w, 0, "W1", WORK) && is(&w, 3, "W3", WORK), "one account's messages pass through as they are");

    MessageSource empty[] = { { MAIN, NULL, 0 }, { WORK, NULL, 0 } };
    merged_message_window_build(&w, empty, 2, MAIN);
    CHECK(w.count == 0, "no messages make an empty conversation");

    merged_message_window_free(&w);
    if (failures == 0) printf("ok: a chat merged across accounts shows each message once, in order, with the account it belongs to\n");
    return failures != 0;
}
