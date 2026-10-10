#ifndef APP_INFRASTRUCTURE_AUTHENTICATED_FILE_CIPHER_H
#define APP_INFRASTRUCTURE_AUTHENTICATED_FILE_CIPHER_H

#include "contracts/i_file_cipher.h"

/* Wraps another file cipher so that a file changed or damaged after it was
 * written is refused before a byte of it is decrypted. What it writes is a
 * short header (a mark, a salt, an HMAC-SHA256 tag) followed by the other
 * cipher's output; the tag covers the mark, the salt and all of that output,
 * under a key derived from the passphrase apart from the encryption key
 * (PBKDF2 with its own salt). A file without the mark is one the other
 * cipher wrote by itself, and is handed to it as it is. Takes ownership of
 * `inner`. */
IFileCipher *authenticated_file_cipher_create(IFileCipher *inner);
/* Whether the file at `path` carries the mark, so its contents were checked when it was decrypted. */
int authenticated_file_cipher_marked(const char *path);

#endif
