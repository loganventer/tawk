#ifndef APP_ENGINES_BANNER_TEXT_H
#define APP_ENGINES_BANNER_TEXT_H

#include <stddef.h>

#include "core/notification.h"

/* The two lines of a banner shown outside tawk: who, and what. The title
 * names the account when there are several; the body is the message when
 * previews are on (the notification then carries it) and "New message"
 * otherwise. Control characters are taken out, since both lines end up in
 * an escape sequence or an argument of another program. */
void banner_text_title(const Notification *n, char *out, size_t size);
void banner_text_body(const Notification *n, char *out, size_t size);
/* Which terminal escape asks for a banner, from the environment: 99 inside
 * kitty, 9 inside iTerm2, WezTerm and Ghostty, 777 elsewhere. */
int  banner_text_osc(const char *term_program, const char *kitty_window, const char *term);

#endif
