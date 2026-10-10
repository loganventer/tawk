#include "clients/tui/lock_screen.h"
#include "clients/tui/tui_palette.h"
#include "engines/idle_lock_policy.h"
#include <ncurses.h>

#include <stdio.h>
#include <string.h>

#ifndef CTRL
#define CTRL(c) ((c) & 0x1f)
#endif

static void forget(LockScreen *l) {
    memset(l->typed, 0, sizeof(l->typed));
    l->length = 0;
}

void lock_screen_lock(LockScreen *l) {
    forget(l);
    l->locked = 1;
    l->failed = 0;
    l->retry_at_ms = 0;
}

void lock_screen_unlock(LockScreen *l) {
    forget(l);
    l->locked = 0;
    l->failed = 0;
    l->retry_at_ms = 0;
}

void lock_screen_take(LockScreen *l, Passphrase *out) {
    l->typed[l->length] = '\0';
    if (passphrase_set(out, l->typed) != 0) memset(out, 0, sizeof(*out));
    forget(l);
}

void lock_screen_wrong(LockScreen *l, int64_t now_ms) {
    l->failed++;
    l->retry_at_ms = now_ms + idle_lock_policy_wait_ms(l->failed);
}

LockKey lock_screen_key(LockScreen *l, int is_key, int ch, int64_t now_ms) {
    if (!l->locked) return LOCK_KEY_NONE;
    if ((is_key && ch == KEY_ENTER) || (!is_key && (ch == '\n' || ch == '\r'))) {
        if (now_ms < l->retry_at_ms || l->length == 0) return LOCK_KEY_CHANGED;
        return LOCK_KEY_SUBMIT;
    }
    if ((is_key && ch == KEY_BACKSPACE) || (!is_key && (ch == 127 || ch == 8))) {
        /* A whole character goes, not one byte of it. */
        while (l->length > 0 && ((unsigned char)l->typed[l->length - 1] & 0xC0) == 0x80) l->length--;
        if (l->length > 0) l->length--;
        l->typed[l->length] = '\0';
        return LOCK_KEY_CHANGED;
    }
    if (!is_key && ch == CTRL('u')) { forget(l); return LOCK_KEY_CHANGED; }
    if (is_key || ch < 32) return LOCK_KEY_NONE;
    /* The character as UTF-8, as the passphrase was typed when the chats were encrypted. */
    char utf8[5];
    int n = 0;
    unsigned int c = (unsigned int)ch;
    if (c < 0x80) utf8[n++] = (char)c;
    else if (c < 0x800) { utf8[n++] = (char)(0xC0 | (c >> 6)); utf8[n++] = (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { utf8[n++] = (char)(0xE0 | (c >> 12)); utf8[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); utf8[n++] = (char)(0x80 | (c & 0x3F)); }
    else { utf8[n++] = (char)(0xF0 | (c >> 18)); utf8[n++] = (char)(0x80 | ((c >> 12) & 0x3F)); utf8[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); utf8[n++] = (char)(0x80 | (c & 0x3F)); }
    if (l->length + n > PASSPHRASE_MAX) return LOCK_KEY_NONE;
    memcpy(l->typed + l->length, utf8, (size_t)n);
    l->length += n;
    return LOCK_KEY_CHANGED;
}

static void centred(int y, int cols, const char *text, int attr) {
    int len = (int)strlen(text);
    int x = cols > len ? (cols - len) / 2 : 0;
    attron(attr);
    mvaddnstr(y, x, text, cols);
    attroff(attr);
}

void lock_screen_draw(const LockScreen *l, int rows, int cols, int64_t now_ms) {
    erase();
    int y = rows / 2 - 2;
    if (y < 0) y = 0;
    centred(y, cols, "tawk is locked", ATTR_BOLD);
    centred(y + 2, cols, "Type the passphrase of your chats and press Enter", ATTR_DIM);
    /* How much was typed, never what. */
    char dots[64];
    int count = 0;
    for (int i = 0; i < l->length; i++) if (((unsigned char)l->typed[i] & 0xC0) != 0x80) count++;
    if (count > 40) count = 40;
    int used = 0;
    for (int i = 0; i < count && used < (int)sizeof(dots) - 2; i++) dots[used++] = '*';
    dots[used] = '\0';
    centred(y + 4, cols, used ? dots : "_", 0);
    if (now_ms < l->retry_at_ms) {
        char wait[96];
        snprintf(wait, sizeof(wait), "That was not it. Try again in %d s", (int)((l->retry_at_ms - now_ms + 999) / 1000));
        centred(y + 6, cols, wait, ATTR_DIM);
    } else if (l->failed > 0) {
        centred(y + 6, cols, "That was not it", ATTR_DIM);
    }
    refresh();
}
