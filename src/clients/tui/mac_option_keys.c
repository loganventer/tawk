#include "clients/tui/mac_option_keys.h"

typedef struct OptionKey {
    int  ch;
    char letter;
    int  also_text;   /* a letter in some language: only outside text */
} OptionKey;

/* Option+E and Option+I are dead keys and never arrive on their own. */
static const OptionKey KEYS[] = {
    { 0x00AC, 'l', 0 },   /* ¬ soft lock */
    { 0x221A, 'v', 0 },   /* √ paste a picture */
    { 0x00AE, 'r', 0 },   /* ® retry a failed message */
    { 0x00B4, 'E', 0 },   /* ´ (Shift+Option+E) edit */
    { 0x00F8, 'o', 1 },   /* ø chat options */
    { 0x00B5, 'm', 1 },   /* µ mute, or the message menu */
    { 0x03C0, 'p', 1 },   /* π pin */
    { 0x00E5, 'a', 1 },   /* å archive */
    { 0x0153, 'q', 1 },   /* œ reply */
    { 0x2020, 't', 0 },   /* † transcripts in this chat */
};

int mac_option_letter(int ch, int typing_text) {
    for (unsigned i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (KEYS[i].ch == ch) return KEYS[i].also_text && typing_text ? 0 : KEYS[i].letter;
    }
    return 0;
}
