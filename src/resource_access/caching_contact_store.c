#include "resource_access/caching_contact_store.h"
#include "utilities/lru_cache.h"

#include <stdlib.h>
#include <string.h>

typedef struct CachingContacts {
    IContactStore *inner;
    LruCache      *cache;
} CachingContacts;

/* Marker for "known missing", so repeated misses do not hit the database. */
static Contact MISSING;

static void free_contact(void *value) {
    if (value != &MISSING) free(value);
}

static int cc_upsert(IContactStore *self, const Contact *contact) {
    CachingContacts *c = self->ctx;
    lru_cache_remove(c->cache, contact->jid);   /* the merge happens in the inner store */
    return c->inner->upsert(c->inner, contact);
}

static int cc_get(IContactStore *self, const char *jid, Contact *out) {
    CachingContacts *c = self->ctx;
    Contact *hit = lru_cache_get(c->cache, jid);
    if (hit == &MISSING) return -1;
    if (hit) { *out = *hit; return 0; }
    if (c->inner->get(c->inner, jid, out) != 0) {
        lru_cache_put(c->cache, jid, &MISSING);
        return -1;
    }
    Contact *copy = malloc(sizeof(*copy));
    if (copy) { *copy = *out; lru_cache_put(c->cache, jid, copy); }
    return 0;
}

static int cc_merge(IContactStore *self, const char *from, const char *to) {
    CachingContacts *c = self->ctx;
    lru_cache_remove(c->cache, from);
    lru_cache_remove(c->cache, to);
    return c->inner->merge(c->inner, from, to);
}

/* A search is not kept: it is rare, and what it finds may change with every upsert. */
static int cc_find_by_name(IContactStore *self, const char *text, Contact *out, int max) {
    CachingContacts *c = self->ctx;
    return c->inner->find_by_name ? c->inner->find_by_name(c->inner, text, out, max) : 0;
}

static void cc_destroy(IContactStore *self) {
    if (!self) return;
    CachingContacts *c = self->ctx;
    lru_cache_destroy(c->cache);
    c->inner->destroy(c->inner);
    free(c);
    free(self);
}

IContactStore *caching_contact_store_create(IContactStore *inner, int capacity) {
    if (!inner) return NULL;
    IContactStore *s = calloc(1, sizeof(*s));
    CachingContacts *c = calloc(1, sizeof(*c));
    if (!s || !c) { free(s); free(c); return inner; }
    c->inner = inner;
    c->cache = lru_cache_create(capacity, free_contact);
    if (!c->cache) { free(s); free(c); return inner; }
    s->ctx = c;
    s->upsert = cc_upsert;
    s->get = cc_get;
    s->merge = cc_merge;
    s->destroy = cc_destroy;
    s->find_by_name = cc_find_by_name;
    return s;
}
