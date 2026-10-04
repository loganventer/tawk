/* The terminal tab title: what it says, and that its spinner is safe to send to any terminal. */
#include "clients/tui/title_flasher.h"
#include "utilities/plain_title.h"
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

    /* Inside GNU screen the title goes out in ASCII only: screen cuts each character down to one byte. */
    char plain[128];
    plain_title("\xE2\xA0\x87 Preparing previews \xC2\xB7 ONS KINDERS", plain, sizeof(plain));
    CHECK(!strcmp(plain, "Preparing previews - ONS KINDERS"), "a plain title drops the spinner and keeps the words");
    plain_title("\xF0\x9F\x9F\xA2 tawk \xC2\xB7 \xD0\x9B\xD0\xB5\xD0\xBD\xD0\xB0 \xC2\xB7 \xF0\x9F\x92\xAC 3", plain, sizeof(plain));
    CHECK(!strcmp(plain, "tawk - - 3"), "a name outside ASCII is left out, not turned into control bytes");
    plain_title("Jan is typing\xE2\x80\xA6 \xC2\xB7 Caf\xC3\xA9", plain, sizeof(plain));
    CHECK(!strcmp(plain, "Jan is typing... - Caf"), "an ellipsis becomes three dots");
    int high = 0;
    for (const char *p = plain; *p; p++) if ((unsigned char)*p >= 0x80 || (unsigned char)*p < 0x20) high++;
    CHECK(high == 0, "nothing but printable ASCII is left");
    char small[8];
    plain_title("Preparing previews", small, sizeof(small));
    CHECK(!strcmp(small, "Prepari"), "a plain title is cut to the room it is given");

    if (failures == 0) printf("ok: the tab title says what is under way, its spinner is safe for any terminal, and inside screen it is plain ASCII\n");
    return failures == 0 ? 0 : 1;
}
