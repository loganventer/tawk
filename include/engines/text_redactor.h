#ifndef APP_ENGINES_TEXT_REDACTOR_H
#define APP_ENGINES_TEXT_REDACTOR_H

/* Hides what a model has no need to read. A card number (13 to 19 digits
 * that pass the card check, spaced or dashed or not) becomes "[card number]".
 * A one-time code (4 to 8 digits standing alone, or two groups of three, in
 * a message that speaks of a code, a PIN, a password or verifying) becomes
 * "[code]". Returns a new string the caller frees, or NULL when there was
 * nothing to hide. */
char *text_redactor_redact(const char *text);

#endif
