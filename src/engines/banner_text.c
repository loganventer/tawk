#include "engines/banner_text.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

/* Also takes out the semicolon's meaning in OSC 777, where it parts title from body. */
static void clean(char *text) {
    str_strip_controls(text);
    for (char *p = text; *p; p++) if (*p == ';') *p = ',';
}

void banner_text_title(const Notification *n, char *out, size_t size) {
    if (n->account_label[0]) snprintf(out, size, "%s (%s)", n->title[0] ? n->title : "tawk", n->account_label);
    else str_copy(out, size, n->title[0] ? n->title : "tawk");
    clean(out);
}

void banner_text_body(const Notification *n, char *out, size_t size) {
    str_copy(out, size, n->body[0] ? n->body : "New message");
    clean(out);
}

int banner_text_osc(const char *term_program, const char *kitty_window, const char *term) {
    if ((kitty_window && kitty_window[0]) || (term && strstr(term, "kitty"))) return 99;
    if (term_program && (!strcmp(term_program, "iTerm.app") || !strcmp(term_program, "WezTerm") || !strcmp(term_program, "ghostty"))) return 9;
    return 777;
}
