#ifndef APP_ENGINES_RECIPIENT_REFERENCE_PARSER_H
#define APP_ENGINES_RECIPIENT_REFERENCE_PARSER_H

/* The JID of the person `ref` names outright: a phone number with its country
 * code ("+27 82 123 4567", "0027821234567", "27821234567") or a personal JID
 * ("27821234567@s.whatsapp.net"). Returns 0 and writes the JID, or -1 for
 * anything else: a name, a group, a broadcast, a channel, or a number written
 * the local way with a leading 0, which names no country. */
int recipient_reference_parse(const char *ref, char *jid, unsigned long size);
/* Whether a JID is one person's, the only kind a first message can be sent to. */
int recipient_reference_is_person(const char *jid);

#endif
