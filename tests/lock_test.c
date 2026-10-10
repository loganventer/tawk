/* The lock that asks for the passphrase of your chats: when there is one,
 * when it locks by itself, how wrong tries are slowed, and what the field keeps. */
#include "clients/tui/lock_screen.h"
#include "core/settings.h"
#include "core/settings_schema.h"
#include "engines/automation_policy.h"
#include "engines/idle_lock_policy.h"

#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); failures++; } } while (0)

static void type(LockScreen *l, const char *text) {
    for (const char *p = text; *p; p++) lock_screen_key(l, 0, (unsigned char)*p, 0);
}

int main(void) {
    CHECK(idle_lock_policy_available(1) && !idle_lock_policy_available(0), "there is a lock only where the chats are encrypted");
    CHECK(!idle_lock_policy_due(1, 0, 99999999) && !idle_lock_policy_due(0, 5, 99999999), "it never locks by itself at 0 minutes, or with plain chats");
    CHECK(!idle_lock_policy_due(1, 5, 5 * 60000 - 1) && idle_lock_policy_due(1, 5, 5 * 60000), "and does at the minute you set");
    CHECK(idle_lock_policy_wait_ms(0) == 0 && idle_lock_policy_wait_ms(2) == 0 && idle_lock_policy_wait_ms(3) == 1000 &&
          idle_lock_policy_wait_ms(4) == 2000 && idle_lock_policy_wait_ms(6) == 8000 && idle_lock_policy_wait_ms(30) == 60000,
          "three wrong tries are free, then the wait doubles from a second up to a minute");

    LockScreen l;
    memset(&l, 0, sizeof(l));
    CHECK(lock_screen_key(&l, 0, 'a', 0) == LOCK_KEY_NONE && l.length == 0, "an unlocked screen takes no keys");
    lock_screen_lock(&l);
    CHECK(l.locked && lock_screen_key(&l, 0, '\n', 0) == LOCK_KEY_CHANGED, "Enter on an empty field checks nothing");
    type(&l, "hunter2");
    CHECK(l.length == 7 && lock_screen_key(&l, 1, KEY_BACKSPACE, 0) == LOCK_KEY_CHANGED && l.length == 6, "typing fills the field and backspace takes one off");
    CHECK(lock_screen_key(&l, 1, KEY_UP, 0) == LOCK_KEY_NONE && lock_screen_key(&l, 0, 3, 0) == LOCK_KEY_NONE && l.length == 6 && l.locked,
          "arrows and control keys do nothing: there is no way round it");
    lock_screen_key(&l, 0, 0xE9, 0);                        /* é */
    CHECK(l.length == 8 && (unsigned char)l.typed[6] == 0xC3 && (unsigned char)l.typed[7] == 0xA9, "a letter beyond ASCII is kept as UTF-8");
    lock_screen_key(&l, 1, KEY_BACKSPACE, 0);
    CHECK(l.length == 6, "and goes in one backspace");
    CHECK(lock_screen_key(&l, 0, '\r', 0) == LOCK_KEY_SUBMIT, "Enter with text asks for it to be checked");
    Passphrase p;
    lock_screen_take(&l, &p);
    CHECK(p.length == 6 && !strcmp(p.text, "hunter") && l.length == 0 && l.typed[0] == '\0' && l.typed[3] == '\0', "what was typed is handed over and wiped from the field");
    lock_screen_wrong(&l, 1000);
    lock_screen_wrong(&l, 1000);
    CHECK(l.locked && l.failed == 2 && l.retry_at_ms == 1000, "a wrong passphrase leaves it locked");
    lock_screen_wrong(&l, 1000);
    type(&l, "x");
    CHECK(l.retry_at_ms == 2000 && lock_screen_key(&l, 0, '\n', 1500) == LOCK_KEY_CHANGED && lock_screen_key(&l, 0, '\n', 2000) == LOCK_KEY_SUBMIT,
          "after the third, Enter is not taken until the wait is over");
    lock_screen_unlock(&l);
    CHECK(!l.locked && l.length == 0 && l.failed == 0, "the right one unlocks and forgets the count");
    lock_screen_lock(&l);
    for (int i = 0; i < PASSPHRASE_MAX + 50; i++) lock_screen_key(&l, 0, 'a', 0);
    CHECK(l.length == PASSPHRASE_MAX, "the field holds a passphrase and no more");

    int found = 0, changeable = 1;
    for (int i = 0; i < settings_schema_count(); i++) {
        const SettingField *f = settings_schema_at(i);
        if (strcmp(f->key, "lock_minutes") == 0) { found = 1; changeable = automation_policy_setting_changeable(f); }
    }
    CHECK(found && !changeable, "an agent cannot change when tawk locks");

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("ok: the passphrase lock\n");
    return 0;
}
