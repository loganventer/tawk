#ifndef APP_CONTRACTS_I_CONTACT_STORE_H
#define APP_CONTRACTS_I_CONTACT_STORE_H

#include "core/contact.h"

typedef struct IContactStore {
    void *ctx;
    /* Merges: empty fields never overwrite known values. */
    int  (*upsert)(struct IContactStore *self, const Contact *contact);
    int  (*get)(struct IContactStore *self, const char *jid, Contact *out);
    /* Copies names known under `from` onto `to` where `to` has none. */
    int  (*merge)(struct IContactStore *self, const char *from, const char *to);
    void (*destroy)(struct IContactStore *self);
    /* The people whose address-book name or chosen name holds `text`, in any
     * case: up to `max` of them, by name. Returns how many, or -1. */
    int  (*find_by_name)(struct IContactStore *self, const char *text, Contact *out, int max);
} IContactStore;

#endif
