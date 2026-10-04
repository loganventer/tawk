#include "clients/tui/title_flasher.h"
#include "utilities/app_info.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

#define FLASH_PERIOD_MS 800

static const char *const CONNECTION_EMOJI[] = {
    "\xF0\x9F\x9F\xA2",   /* 🟢 online */
    "\xF0\x9F\x9F\xA1",   /* 🟡 connecting */
    "\xF0\x9F\x94\xB4",   /* 🔴 unavailable */
    "\xF0\x9F\x94\x97"    /* 🔗 not linked */
};

void title_flasher_init(TitleFlasher *f, ITerminalTitle *title) {
    memset(f, 0, sizeof(*f));
    f->title = title;
}

void title_flasher_start(TitleFlasher *f, int64_t now_ms) {
    if (!f->flashing_since_ms) f->flashing_since_ms = now_ms;
}

void title_flasher_acknowledge(TitleFlasher *f) { f->flashing_since_ms = 0; }

/* A braille spinner, one frame per animation tick, while something is under way.
 * No frame has a byte from 0x80 to 0x9F: a terminal or multiplexer that reads those as
 * control codes ends the title there and prints the rest of it on the screen. */
static const char *const SPINNER[] = {
    "\xE2\xA3\xBE", "\xE2\xA3\xBD", "\xE2\xA3\xBB", "\xE2\xA2\xBF",   /* ⣾ ⣽ ⣻ ⢿ */
    "\xE2\xA1\xBF", "\xE2\xA3\xAF", "\xE2\xA3\xB7",                    /* ⡿ ⣯ ⣷ */
};
#define SPINNER_FRAMES ((int64_t)(sizeof(SPINNER) / sizeof(SPINNER[0])))

/* Someone typing to you: a pen and a speech bubble take turns in front,
 * and the dots after the words count up and start again, so the tab moves
 * like the typing dots in a chat. */
static void typing_head(char *out, size_t size, const char *activity, const char *flags, int64_t now_ms) {
    static const char *const LEAD[] = { "\xE2\x9C\x8D\xEF\xB8\x8F", "\xF0\x9F\x92\xAC" };   /* ✍️ 💬 */
    static const char *const DOTS[] = { "", ".", "..", "..." };
    char words[256];
    str_copy(words, sizeof(words), activity);
    size_t n = strlen(words);
    if (n >= 3 && strcmp(words + n - 3, "\xE2\x80\xA6") == 0) words[n - 3] = '\0';   /* the static … */
    else while (n > 0 && words[n - 1] == '.') words[--n] = '\0';
    snprintf(out, size, "%s %s%s%s", LEAD[(now_ms / 700) % 2], words, DOTS[(now_ms / 350) % 4], flags);
}

/* The tab title follows what is going on, most useful part first:
 *   "⣾ Loading older messages · Dev team · 💬 3"
 *   "🟢 tawk · Mom · 💬 3  📷 1"
 * with do not disturb, recording and playing flags, and a flash between
 * the counts and "✉ 3 new" while new messages wait. */
void title_flasher_tick(TitleFlasher *f, int64_t now_ms, const TabStatus *st, const Settings *s) {
    if (!f->title) return;
    int c = st->connection >= 0 && st->connection <= 3 ? st->connection : 2;
    const char *lead = st->busy ? SPINNER[(now_ms / 120) % SPINNER_FRAMES] : CONNECTION_EMOJI[c];
    char flags[64];
    snprintf(flags, sizeof(flags), "%s%s%s", st->dnd ? " \xF0\x9F\x94\x95" : "",
             st->recording ? " \xF0\x9F\x94\xB4" : "", st->playing ? " \xF0\x9F\x8E\xA7" : "");

    char head[320];
    if (st->typing && st->activity && *st->activity) typing_head(head, sizeof(head), st->activity, flags, now_ms);
    else if (st->activity && *st->activity) snprintf(head, sizeof(head), "%s %s%s", lead, st->activity, flags);
    else snprintf(head, sizeof(head), "%s %s%s", lead, APP_NAME, flags);

    char where[160] = "";
    if (st->chat_name && *st->chat_name) snprintf(where, sizeof(where), " \xC2\xB7 %s", st->chat_name);
    else if (st->user_name && *st->user_name && !(st->activity && *st->activity)) snprintf(where, sizeof(where), " \xC2\xB7 %s", st->user_name);

    char counts[256] = "", tail[280] = "";
    int total = unread_tally_total(st->tally);
    unread_tally_format(st->tally, counts, sizeof(counts));
    int flash_off = total > 0 && s->title_flash && f->flashing_since_ms &&
                    ((now_ms - f->flashing_since_ms) / FLASH_PERIOD_MS) % 2 == 1;
    if (flash_off) snprintf(tail, sizeof(tail), " \xC2\xB7 \xE2\x9C\x89 %d new", total);
    else if (total > 0) snprintf(tail, sizeof(tail), " \xC2\xB7 %s", counts);

    char text[600];
    snprintf(text, sizeof(text), "%s%s%s", head, where, tail);
    if (strcmp(text, f->last) != 0) {
        f->title->set(f->title, text);
        str_copy(f->last, sizeof(f->last), text);
    }
    if (f->title->progress) f->title->progress(f->title, st->progress);
}
