#ifndef APP_CORE_ACCOUNT_ID_H
#define APP_CORE_ACCOUNT_ID_H

/* Which of your WhatsApp accounts something belongs to. An id is given once,
 * never changes and is never used again after its account is removed. */
typedef int AccountId;

#define ACCOUNT_ID_NONE  0
#define ACCOUNT_ID_FIRST 1   /* the account an install had before it could hold several */

#endif
