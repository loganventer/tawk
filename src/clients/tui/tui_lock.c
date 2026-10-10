/* Locking tawk until the passphrase of your chats is typed: on Ctrl+L or
 * /lock, and by itself after the minutes you set. Only where the chats are
 * encrypted. tawk keeps running behind the lock: messages arrive and agents
 * are served, and nothing of it is drawn. */
#include "tui_app_state.h"
#include "engines/idle_lock_policy.h"
#include "utilities/clock_util.h"

#include <ncurses.h>

static int encrypted(TuiApp *app) {
    return app->deps.crypt && database_crypt_manager_is_encrypted(app->deps.crypt);
}

int tui_app_locked(const TuiApp *app) { return app->lock.locked; }

int tui_app_lock(TuiApp *app) {
    if (!idle_lock_policy_available(encrypted(app))) return 0;
    if (!app->lock.locked) {
        lock_screen_lock(&app->lock);
        app->contact.open = 0;                               /* nothing is left open behind it */
        clearok(curscr, TRUE);
    }
    app->dirty = 1;
    return 1;
}

void tui_app_lock_tick(TuiApp *app, int64_t now_ms) {
    if (app->lock.locked) {
        if (app->lock.retry_at_ms && now_ms < app->lock.retry_at_ms + 1000) app->dirty = 1;    /* the countdown */
        return;
    }
    if (media_manager_is_recording(app->deps.media) || tui_app_show_login(app)) return;
    int minutes = settings_manager_current(app->deps.settings)->lock_minutes;
    if (idle_lock_policy_due(encrypted(app), minutes, now_ms - app->last_key_ms)) tui_app_lock(app);
}

void tui_app_lock_key(TuiApp *app, int is_key, int ch) {
    int64_t now = clock_now_ms();
    LockKey key = lock_screen_key(&app->lock, is_key, ch, now);
    if (key == LOCK_KEY_NONE) return;
    app->dirty = 1;
    if (key != LOCK_KEY_SUBMIT) return;
    Passphrase typed;
    lock_screen_take(&app->lock, &typed);
    int opens = typed.length > 0 && database_crypt_manager_unlocks(app->deps.crypt, &typed) == 1;
    passphrase_wipe(&typed);
    if (!opens) { lock_screen_wrong(&app->lock, clock_now_ms()); return; }
    lock_screen_unlock(&app->lock);
    clearok(curscr, TRUE);
    sixel_overlay_invalidate(&app->sixel_overlay);
    app->last_key_ms = clock_now_ms();
}
