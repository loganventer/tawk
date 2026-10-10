#ifndef APP_CLIENTS_TUI_LOCK_SCREEN_H
#define APP_CLIENTS_TUI_LOCK_SCREEN_H

#include <stdint.h>

#include "core/passphrase.h"

/* The screen that hides everything until the passphrase of your chats is
 * typed. It draws nothing of tawk: no chat, no name, no count. */
typedef struct LockScreen {
    int     locked;
    char    typed[PASSPHRASE_MAX + 1];
    int     length;
    int     failed;          /* wrong tries since it locked */
    int64_t retry_at_ms;     /* no try is taken before this */
} LockScreen;

typedef enum {
    LOCK_KEY_NONE = 0,       /* nothing to do */
    LOCK_KEY_CHANGED,        /* draw again */
    LOCK_KEY_SUBMIT          /* Enter: check what was typed */
} LockKey;

void    lock_screen_lock(LockScreen *lock);
LockKey lock_screen_key(LockScreen *lock, int is_key, int ch, int64_t now_ms);
/* Hands over what was typed and clears the field. */
void    lock_screen_take(LockScreen *lock, Passphrase *out);
/* The passphrase was wrong: counts it and sets the wait. Right: unlocks and forgets everything. */
void    lock_screen_wrong(LockScreen *lock, int64_t now_ms);
void    lock_screen_unlock(LockScreen *lock);
void    lock_screen_draw(const LockScreen *lock, int rows, int cols, int64_t now_ms);

#endif
