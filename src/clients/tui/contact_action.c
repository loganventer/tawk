#include "clients/tui/contact_action.h"

static const char *const LABELS[CONTACT_ACTION_COUNT] = {
    "\xF0\x9F\x96\xBC  View profile picture",
    "\xF0\x9F\x94\x8D  Search messages",
    "\xE2\x9A\x99  Chat options (mute, pin, theme, tone)",
    "\xF0\x9F\x99\x88  Soft-lock chat",
    "\xF0\x9F\x93\xA4  Export chat",
    "\xF0\x9F\x93\xA6  Export chat with media",
    "\xF0\x9F\x9A\xAB  Block",
    "\xE2\x9C\x85  Unblock",
    "\xF0\x9F\xA7\xB9  Clear chat\xE2\x80\xA6",
    "\xF0\x9F\x97\x91  Delete chat\xE2\x80\xA6",
    "\xF0\x9F\x93\xA4  Send from",
    "\xF0\x9F\x94\x97  Merge across my numbers",
    "\xF0\x9F\xA4\x96  Agents answer by themselves here",
    "\xF0\x9F\x93\x9D  Show transcripts",
    "\xF0\x9F\x8E\x99  Transcribe voice notes",
    "\xF0\x9F\x97\xA3  Voice note languages\xE2\x80\xA6",
    "\xE2\x9C\x82  TL;DR (summarise messages)",
    "\xF0\x9F\x92\xAC  This is my chat with the agent",
};

const char *contact_action_label(ContactAction a) {
    return (a >= 0 && a < CONTACT_ACTION_COUNT) ? LABELS[a] : "";
}

int contact_action_needs_confirmation(ContactAction a) {
    return a == CONTACT_ACTION_BLOCK || a == CONTACT_ACTION_CLEAR || a == CONTACT_ACTION_DELETE;
}
