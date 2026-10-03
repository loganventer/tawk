#include "engines/jid_list.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Calls `each` for every item of the list, trimmed; stops when it returns non-zero. */
static int walk(const char *list, int (*each)(const char *item, size_t len, void *ctx), void *ctx) {
    const char *p = list ? list : "";
    while (*p) {
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        size_t len = strcspn(p, ",");
        size_t trimmed = len;
        while (trimmed > 0 && isspace((unsigned char)p[trimmed - 1])) trimmed--;
        if (trimmed > 0 && each(p, trimmed, ctx)) return 1;
        p += len;
    }
    return 0;
}

static int names(const char *item, size_t len, void *ctx) {
    const char *jid = ctx;
    return (len == 1 && item[0] == '*') || (strlen(jid) == len && strncmp(item, jid, len) == 0);
}

int jid_list_contains(const char *list, const char *jid) {
    return jid && jid[0] ? walk(list, names, (void *)jid) : 0;
}

typedef struct Copy {
    const char *skip;       /* the JID being taken out */
    char       *out;
    size_t      size;
    size_t      used;
    int         overflow;
} Copy;

static void append(Copy *c, const char *item, size_t len) {
    size_t need = len + (c->used ? 1 : 0);
    if (c->used + need + 1 > c->size) { c->overflow = 1; return; }
    if (c->used) c->out[c->used++] = ',';
    memcpy(c->out + c->used, item, len);
    c->used += len;
    c->out[c->used] = '\0';
}

static int keep(const char *item, size_t len, void *ctx) {
    Copy *c = ctx;
    if (strlen(c->skip) == len && strncmp(item, c->skip, len) == 0) return 0;
    append(c, item, len);
    return 0;
}

int jid_list_set(const char *list, const char *jid, int on, char *out, size_t size) {
    if (!size || !jid || !jid[0]) return -1;
    out[0] = '\0';
    Copy c = { jid, out, size, 0, 0 };
    walk(list, keep, &c);                                  /* everything but this JID */
    if (on) append(&c, jid, strlen(jid));
    return c.overflow ? -1 : 0;
}
