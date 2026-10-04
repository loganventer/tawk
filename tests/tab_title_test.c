/* The terminal tab title: what it says, and that its spinner is safe to send to any terminal. */
#include "clients/tui/title_flasher.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static char shown[600];
static int writes;

static void fake_set(ITerminalTitle *self, const char *title) {
    (void)self;
    str_copy(shown, sizeof(shown), title);
    writes++;
}

static ITerminalTitle title = { NULL, fake_set, NULL, NULL, NULL };

/* Bytes from 0x80 to 0x9F are control codes to a terminal that does not read its input as UTF-8. */
static int control_bytes(const char *text, size_t length) {
    int n = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x80 && c <= 0x9F) n++;
    }
    return n;
}

int main(void) {
    Settings settings;
    memset(&settings, 0, sizeof(settings));
    UnreadTally tally;
    memset(&tally, 0, sizeof(tally));
    TitleFlasher flasher;
    title_flasher_init(&flasher, &title);

    TabStatus busy = { .user_name = "Logan", .tally = &tally, .chat_name = "ONS KINDERS", .activity = "Preparing previews", .busy = 1 };
    int unsafe = 0, frames = 0;
    char previous[600] = "";
    for (int64_t now = 0; now < 4000; now += 40) {
        title_flasher_tick(&flasher, now, &busy, &settings);
        const char *words = strstr(shown, " Preparing previews");
        CHECK(words && strstr(shown, "ONS KINDERS"), "a busy title says what is under way and in which chat");
        if (!words) break;
        unsafe += control_bytes(shown, (size_t)(words - shown));
        if (strcmp(previous, shown) != 0) frames++;
        str_copy(previous, sizeof(previous), shown);
    }
    CHECK(unsafe == 0, "no spinner frame holds a byte a terminal could read as a control code");
    CHECK(frames > 3, "the spinner moves while something is under way");

    int before = writes;
    TabStatus idle = { .user_name = "Logan", .tally = &tally, .chat_name = "", .activity = "" };
    title_flasher_tick(&flasher, 5000, &idle, &settings);
    title_flasher_tick(&flasher, 5040, &idle, &settings);
    CHECK(writes == before + 1 && strstr(shown, "Logan"), "an idle title is written once and names you");

    if (failures == 0) printf("ok: the tab title says what is under way, and its spinner is safe for any terminal\n");
    return failures == 0 ? 0 : 1;
}
