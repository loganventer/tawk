#include "clients/tui/message_action.h"

static const char *const LABELS[MESSAGE_ACTION_COUNT] = {
    "\xE2\x86\xA9  Reply",
    "\xF0\x9F\x98\x80  React",
    "\xE2\x9C\x8F  Edit",
    "\xF0\x9F\x93\x8B  Copy text",
    "\xE2\x86\xAA  Forward\xE2\x80\xA6",
    "\xE2\x86\x97  Open / play",
    "\xF0\x9F\x93\x96  Read in full",
    "\xF0\x9F\x93\x9D  Show transcript",
    "\xE2\x9C\x82  TL;DR: original / summary",
    "\xE2\x86\xBB  Retry sending",
    "\xF0\x9F\x92\xBE  Save to Downloads",
    "\xE2\x86\xA5  Go to quoted message",
    "\xE2\x84\xB9  Message info",
    "\xF0\x9F\x97\x91  Delete\xE2\x80\xA6",
    "\xF0\x9F\x97\x91  Delete for me",
    "\xF0\x9F\x97\x91  Delete for everyone",
    "\xE2\x9C\x95  Cancel",
};

const char *message_action_label(MessageAction a) {
    return (a >= 0 && a < MESSAGE_ACTION_COUNT) ? LABELS[a] : "";
}
