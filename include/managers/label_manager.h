#ifndef APP_MANAGERS_LABEL_MANAGER_H
#define APP_MANAGERS_LABEL_MANAGER_H

#include <stddef.h>

#include "contracts/i_label_store.h"

#define LABELS_MAX 64       /* different labels shown in a list */

/* Your own labels on chats. A label exists while some chat carries it. They
 * are kept on this computer and hold for a person or group on all your numbers. */
typedef struct LabelManager LabelManager;

LabelManager *label_manager_create(ILabelStore *store);
void          label_manager_destroy(LabelManager *mgr);

/* Whether `jid` carries `label`. */
int  label_manager_has(LabelManager *mgr, const char *jid, const char *label);
/* Puts `label` on `jid` when it is not there and takes it off when it is.
 * Returns 1 when it is on afterwards, 0 when off, -1 when the name is not
 * usable or it could not be kept; the name as kept is written to `clean`. */
int  label_manager_toggle(LabelManager *mgr, const char *jid, const char *label, char clean[CHAT_LABEL_SIZE]);
/* The labels of one chat as "work, family"; "" when it has none. */
void label_manager_of(LabelManager *mgr, const char *jid, char *out, size_t size);
/* Every label in use, in alphabetical order. Returns how many. */
int  label_manager_all(LabelManager *mgr, char out[][CHAT_LABEL_SIZE], int max);

#endif
