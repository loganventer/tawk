#ifndef APP_CLIENTS_TUI_MERGED_MESSAGE_WINDOW_H
#define APP_CLIENTS_TUI_MERGED_MESSAGE_WINDOW_H

#include "clients/tui/message_source.h"

/* One conversation made of the same contact's chats in several accounts:
 * their messages in the order they happened, each remembering the account
 * it belongs to. A message that reached several accounts (a group all of
 * them are in) appears once.
 *
 * The messages are borrowed: they point into the sources and are good only
 * until a source changes. Nothing here is to be disposed of. */
typedef struct MergedMessageWindow {
    Message   *items;
    AccountId *owners;
    int        count;
    int        capacity;
} MergedMessageWindow;

void merged_message_window_init(MergedMessageWindow *window);
void merged_message_window_free(MergedMessageWindow *window);
/* A message held by several sources is taken from `prefer` when it has it. */
void merged_message_window_build(MergedMessageWindow *window, const MessageSource *sources, int source_count, AccountId prefer);

#endif
