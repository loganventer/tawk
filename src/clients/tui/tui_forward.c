/* Forwarding: the chat picker, carried out through the messaging manager. */
#include "tui_app_state.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

void tui_app_open_forward(TuiApp *app, int index) {
    int count = 0;
    const Message *msgs = tui_app_messages(app, &count);
    if (index < 0 || index >= count) return;
    str_copy(app->forward_id, sizeof(app->forward_id), msgs[index].id);
    chat_picker_open(&app->forward_picker, "Forward to");
    app->dirty = 1;
}

void tui_app_forward_request(TuiApp *app, PopupResult result) {
    app->dirty = 1;
    if (result != POPUP_CHOSEN) return;
    const char *jids[CHAT_PICKER_MAX_CHOSEN];
    int n = chat_picker_chosen(&app->forward_picker, jids);
    int sent = messaging_manager_forward(app->deps.messaging, app->forward_id, jids, n);
    app->forward_id[0] = '\0';
    char msg[96];
    if (sent <= 0) snprintf(msg, sizeof(msg), "This message could not be forwarded");
    else if (sent == 1) snprintf(msg, sizeof(msg), "\xE2\x86\xAA Forwarded");
    else snprintf(msg, sizeof(msg), "\xE2\x86\xAA Forwarded to %d chats", sent);
    tui_app_toast(app, msg, sent <= 0);
}
