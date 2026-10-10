#ifndef APP_ENGINES_SELF_CHAT_RULE_H
#define APP_ENGINES_SELF_CHAT_RULE_H

/* Whether `chat_jid` is the "message yourself" chat of the account whose own
 * JID is `own_jid`: a chat with one person, and that person is you. The
 * device part of your own JID ("27821234567:12@...") is not part of who you are. */
int self_chat_rule_is(const char *own_jid, const char *chat_jid);

#endif
