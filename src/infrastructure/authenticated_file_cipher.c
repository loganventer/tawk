#include "infrastructure/authenticated_file_cipher.h"
#include "utilities/sha256.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MARK       "TAWKBAK3\n"
#define MARK_SIZE  9
#define SALT_SIZE  16
#define HEAD_SIZE  (MARK_SIZE + SALT_SIZE + SHA256_SIZE)
#define ITERATIONS 600000
#define KEY_NOTE   "tawk backup tag"       /* keeps this key apart from any other made from the same passphrase */

static IFileCipher *inner_of(IFileCipher *self) { return (IFileCipher *)self->ctx; }

static void mac_key(const Passphrase *p, const uint8_t salt[SALT_SIZE], uint8_t key[SHA256_SIZE]) {
    uint8_t salted[sizeof(KEY_NOTE) - 1 + SALT_SIZE];
    memcpy(salted, KEY_NOTE, sizeof(KEY_NOTE) - 1);
    memcpy(salted + sizeof(KEY_NOTE) - 1, salt, SALT_SIZE);
    pbkdf2_hmac_sha256(p->text, p->length, salted, sizeof(salted), ITERATIONS, key, SHA256_SIZE);
}

static int random_bytes(uint8_t *out, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, out + got, len - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    return got == len ? 0 : -1;
}

/* Feeds the rest of `in` to the tag, copying it to `out` when one is given. */
static int pump(FILE *in, FILE *out, HmacSha256 *mac) {
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        hmac_sha256_update(mac, buf, n);
        if (out && fwrite(buf, 1, n, out) != n) return -1;
    }
    return ferror(in) ? -1 : 0;
}

static void temp_beside(const char *path, const char *suffix, char *out, size_t size) {
    snprintf(out, size, "%s.%s.%d", path, suffix, (int)getpid());
}

static int auth_encrypt(IFileCipher *self, const char *in, const char *out, const Passphrase *p) {
    char body[4096];
    temp_beside(out, "body", body, sizeof(body));
    if (inner_of(self)->encrypt(inner_of(self), in, body, p) != 0) { unlink(body); return -1; }
    uint8_t salt[SALT_SIZE], key[SHA256_SIZE], tag[SHA256_SIZE];
    int rc = -1;
    FILE *from = fopen(body, "rb");
    int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    FILE *to = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (from && to && random_bytes(salt, sizeof(salt)) == 0) {
        mac_key(p, salt, key);
        HmacSha256 mac;
        hmac_sha256_init(&mac, key, sizeof(key));
        hmac_sha256_update(&mac, MARK, MARK_SIZE);
        hmac_sha256_update(&mac, salt, SALT_SIZE);
        uint8_t blank[SHA256_SIZE] = { 0 };
        /* The tag's place is written first and filled in once the whole body has gone past. */
        if (fwrite(MARK, 1, MARK_SIZE, to) == MARK_SIZE && fwrite(salt, 1, SALT_SIZE, to) == SALT_SIZE &&
            fwrite(blank, 1, SHA256_SIZE, to) == SHA256_SIZE && pump(from, to, &mac) == 0) {
            hmac_sha256_final(&mac, tag);
            if (fseek(to, MARK_SIZE + SALT_SIZE, SEEK_SET) == 0 && fwrite(tag, 1, SHA256_SIZE, to) == SHA256_SIZE && fflush(to) == 0) rc = 0;
        }
        memset(key, 0, sizeof(key));
    }
    if (from) fclose(from);
    if (to) fclose(to);
    else if (fd >= 0) close(fd);
    unlink(body);
    if (rc != 0) unlink(out);                               /* never leave half a file */
    return rc;
}

int authenticated_file_cipher_marked(const char *path) {
    char head[MARK_SIZE];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int marked = fread(head, 1, MARK_SIZE, f) == MARK_SIZE && memcmp(head, MARK, MARK_SIZE) == 0;
    fclose(f);
    return marked;
}

static int auth_decrypt(IFileCipher *self, const char *in, const char *out, const Passphrase *p) {
    if (!authenticated_file_cipher_marked(in)) return inner_of(self)->decrypt(inner_of(self), in, out, p);   /* written before the tag existed */
    uint8_t head[HEAD_SIZE], key[SHA256_SIZE], tag[SHA256_SIZE];
    char body[4096];
    temp_beside(out, "body", body, sizeof(body));
    int rc = -1;
    FILE *from = fopen(in, "rb");
    int fd = open(body, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    FILE *to = fd >= 0 ? fdopen(fd, "wb") : NULL;
    if (from && to && fread(head, 1, HEAD_SIZE, from) == HEAD_SIZE) {
        mac_key(p, head + MARK_SIZE, key);
        HmacSha256 mac;
        hmac_sha256_init(&mac, key, sizeof(key));
        hmac_sha256_update(&mac, head, MARK_SIZE + SALT_SIZE);
        int copied = pump(from, to, &mac) == 0 && fflush(to) == 0;
        hmac_sha256_final(&mac, tag);
        memset(key, 0, sizeof(key));
        /* Only a file whose every byte is as tawk wrote it is handed on to be decrypted. */
        if (copied && sha256_equal(tag, head + MARK_SIZE + SALT_SIZE, SHA256_SIZE)) rc = 0;
    }
    if (from) fclose(from);
    if (to) fclose(to);
    else if (fd >= 0) close(fd);
    if (rc == 0) rc = inner_of(self)->decrypt(inner_of(self), body, out, p);
    unlink(body);
    return rc == 0 ? 0 : -1;
}

static int auth_checked(IFileCipher *self, const char *in) { (void)self; return authenticated_file_cipher_marked(in); }

static int auth_available(IFileCipher *self) { return inner_of(self)->available(inner_of(self)); }

static void auth_destroy(IFileCipher *self) {
    inner_of(self)->destroy(inner_of(self));
    free(self);
}

IFileCipher *authenticated_file_cipher_create(IFileCipher *inner) {
    if (!inner) return NULL;
    IFileCipher *c = calloc(1, sizeof(*c));
    if (!c) { inner->destroy(inner); return NULL; }
    c->ctx = inner;
    c->encrypt = auth_encrypt;
    c->decrypt = auth_decrypt;
    c->checked = auth_checked;
    c->available = auth_available;
    c->destroy = auth_destroy;
    return c;
}
