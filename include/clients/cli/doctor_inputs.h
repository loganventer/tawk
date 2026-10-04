#ifndef APP_CLIENTS_CLI_DOCTOR_INPUTS_H
#define APP_CLIENTS_CLI_DOCTOR_INPUTS_H

#include "core/account.h"
#include "contracts/i_audio_backend.h"
#include "contracts/i_emoji_catalog.h"
#include "core/settings.h"

/* Everything the setup check looks at, resolved by the composition root. */
typedef struct DoctorInputs {
    const Settings *settings;
    const char     *config_path;
    const char     *db_path;
    const char     *log_path;
    const char     *themes_dir;       /* bundled themes */
    const char     *sidecar_dir;
    IEmojiCatalog  *emoji;
    IAudioBackend  *audio;
    int             whatsmeow_built;  /* the in-process backend is compiled in */
    const char     *backend;          /* configured: whatsmeow or baileys */
    int             sqlcipher_built;  /* this build can encrypt the database */
    int             db_encrypted;     /* the database is encrypted */
    const Account  *accounts;         /* as the database lists them; NULL when it could not be read */
    int             account_count;
} DoctorInputs;

#endif
