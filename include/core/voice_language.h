#ifndef APP_CORE_VOICE_LANGUAGE_H
#define APP_CORE_VOICE_LANGUAGE_H

/* A language a voice note can be spoken in, as a transcriber names it. */
typedef struct VoiceLanguage {
    const char *code;     /* "af" */
    const char *name;     /* "Afrikaans" */
} VoiceLanguage;

/* The languages a transcriber can tell apart, by name in alphabetical order of their codes. */
int                  voice_language_count(void);
const VoiceLanguage *voice_language_at(int index);
/* The language with that code, or NULL. */
const VoiceLanguage *voice_language_find(const char *code);

#endif
