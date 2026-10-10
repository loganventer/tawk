#ifndef APP_CLIENTS_CONTROL_CONTROL_ALLOWANCE_H
#define APP_CLIENTS_CONTROL_CONTROL_ALLOWANCE_H

/* "Allow this again for this session": one operation in one chat. The chat is a JID,
 * with its account in front ("2/...") when that is not the first one. */
#define CONTROL_ALLOWANCE_KEY_SIZE 144

typedef struct ControlAllowance {
    char op[32];
    char chat_jid[CONTROL_ALLOWANCE_KEY_SIZE];
} ControlAllowance;

#endif
