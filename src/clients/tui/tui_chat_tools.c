/* Tools for a long chat list: narrowing it to unread chats, groups, direct
 * chats, chats awaiting a reply, snoozed chats or one label; your own
 * labels; and putting a chat aside until a time or until its person writes. */
#include "tui_app_state.h"
#include "core/chat_filter_facts.h"
#include "core/notification.h"
#include "engines/chat_filter.h"
#include "engines/reminder_rule.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AWAITING_MAX        512
#define AWAITING_REFRESH_MS 60000
#define REMINDERS_EVERY_MS  1000

void tui_app_chat_tools_free(TuiApp *app) {
    free(app->chat_hidden);
    free(app->awaiting);
    app->chat_hidden = NULL;
    app->awaiting = NULL;
}

/* The chats where your last message is unanswered, across your accounts, looked up now and then. */
static void refresh_awaiting(TuiApp *app, int64_t now) {
    if (app->narrow != CHAT_FILTER_AWAITING) return;
    if (app->awaiting && now - app->awaiting_at_ms < AWAITING_REFRESH_MS) return;
    if (!app->awaiting) app->awaiting = calloc(AWAITING_MAX, sizeof(*app->awaiting));
    if (!app->awaiting) return;
    int days = settings_manager_current(app->deps.settings)->awaiting_days;
    IAccountDirectory *dir = app->deps.directory;
    int accounts = tui_app_account_count(app), n = 0;
    if (!dir || accounts <= 1) {
        n = messaging_manager_awaiting(app->deps.messaging, days, app->awaiting, AWAITING_MAX);
    } else {
        for (int i = 0; i < accounts && n < AWAITING_MAX; i++) {
            const AccountServices *sv = dir->at(dir, i);
            if (sv) n += messaging_manager_awaiting(sv->messaging, days, app->awaiting + n, AWAITING_MAX - n);
        }
    }
    app->awaiting_count = n;
    app->awaiting_at_ms = now;
}

static int is_awaiting(const TuiApp *app, const char *jid) {
    for (int i = 0; i < app->awaiting_count; i++) if (strcmp(app->awaiting[i], jid) == 0) return 1;
    return 0;
}

void tui_app_narrow_rows(TuiApp *app, const Chat *chats, int count) {
    ChatListView *v = &app->chat_list;
    if (v->widen_asked) {                                   /* Enter on "Showing: ..." */
        v->widen_asked = 0;
        app->narrow = CHAT_FILTER_NONE;
        app->narrow_label[0] = '\0';
    }
    chat_filter_title(app->narrow, app->narrow_label, v->narrowed, sizeof(v->narrowed));
    int snoozed_any = app->deps.reminders && reminder_manager_count(app->deps.reminders) > 0;
    if (app->narrow == CHAT_FILTER_NONE && !snoozed_any) { v->hidden = NULL; v->hidden_count = 0; return; }
    if (count > app->chat_hidden_size) {
        unsigned char *grown = realloc(app->chat_hidden, (size_t)count);
        if (!grown) { v->hidden = NULL; v->hidden_count = 0; return; }
        app->chat_hidden = grown;
        app->chat_hidden_size = count;
    }
    refresh_awaiting(app, clock_now_ms());
    for (int i = 0; i < count; i++) {
        ChatFilterFacts facts = {
            .awaiting = app->narrow == CHAT_FILTER_AWAITING && is_awaiting(app, chats[i].jid),
            .snoozed = snoozed_any && reminder_manager_snoozed(app->deps.reminders, chats[i].jid, NULL),
            .labelled = app->narrow == CHAT_FILTER_LABEL && app->deps.labels && label_manager_has(app->deps.labels, chats[i].jid, app->narrow_label),
        };
        app->chat_hidden[i] = !chat_filter_shows(app->narrow, &chats[i], &facts);
    }
    v->hidden = app->chat_hidden;
    v->hidden_count = count;
}

void tui_app_set_narrow(TuiApp *app, const char *args) {
    ChatFilterKind kind;
    char label[CHAT_LABEL_SIZE];
    if (chat_filter_parse(args, &kind, label) != 0) {
        tui_app_toast(app, "/filter takes unread, groups, direct, awaiting, snoozed, label NAME, or off", 1);
        return;
    }
    if (kind == CHAT_FILTER_LABEL && !app->deps.labels) return;
    app->narrow = kind;
    str_copy(app->narrow_label, sizeof(app->narrow_label), label);
    app->awaiting_at_ms = 0;                                /* looked up afresh */
    app->chat_list.selected = app->chat_list.scroll = 0;
    app->focus = TUI_FOCUS_CHATS;
    app->dirty = 1;
    char title[64], note[160];
    chat_filter_title(kind, label, title, sizeof(title));
    if (kind == CHAT_FILTER_NONE) snprintf(note, sizeof(note), "Showing every chat");
    else snprintf(note, sizeof(note), "Showing: %s. /filter off, or Enter on the first row, shows every chat again.", title);
    tui_app_toast(app, note, 0);
}

void tui_app_toggle_label(TuiApp *app, const char *jid, const char *label) {
    LabelManager *mgr = app->deps.labels;
    if (!mgr || !jid) return;
    char clean[CHAT_LABEL_SIZE], note[160];
    int on = label_manager_toggle(mgr, jid, label, clean);
    if (on < 0) { tui_app_toast(app, "A label is up to 24 characters, with no comma or slash", 1); return; }
    snprintf(note, sizeof(note), on ? "\xF0\x9F\x8F\xB7 Labelled %s. /filter label %s shows the chats that carry it." : "Label %s taken off this chat", clean, clean);
    tui_app_toast(app, note, 0);
    tui_app_refresh_tools_prefs(app);
    app->dirty = 1;
}

void tui_app_list_labels(TuiApp *app) {
    LabelManager *mgr = app->deps.labels;
    if (!mgr) return;
    char all[LABELS_MAX][CHAT_LABEL_SIZE], note[512];
    int n = label_manager_all(mgr, all, LABELS_MAX);
    if (n == 0) { tui_app_toast(app, "No labels yet. /label NAME puts one on the open chat.", 0); return; }
    size_t used = (size_t)snprintf(note, sizeof(note), "Labels: ");
    for (int i = 0; i < n && used < sizeof(note); i++) used += (size_t)snprintf(note + used, sizeof(note) - used, "%s%s", i ? ", " : "", all[i]);
    tui_app_toast(app, note, 0);
}

static void when_text(int64_t due, char *out, size_t size) {
    if (due == 0) { str_copy(out, size, "until they write"); return; }
    time_t t = (time_t)due;
    struct tm tm_due;
    localtime_r(&t, &tm_due);
    strftime(out, size, "until %a %d %b %H:%M, or sooner if they write", &tm_due);
}

void tui_app_remind(TuiApp *app, const char *jid, const char *args) {
    ReminderManager *mgr = app->deps.reminders;
    if (!mgr || !jid) return;
    int64_t now = (int64_t)time(NULL), due = 0;
    const char *text = args ? args : "";
    while (*text == ' ') text++;
    if (!strcmp(text, "off") || !strcmp(text, "cancel")) {
        reminder_manager_clear(mgr, jid);
        tui_app_toast(app, "This chat is back in the list", 0);
    } else if (reminder_rule_parse(text, now, &due) != 0) {
        tui_app_toast(app, "/remind takes a time (9:00, tomorrow, +2h, fri 17:30), reply, or off", 1);
        return;
    } else if (reminder_manager_set(mgr, jid, due, now) != 0) {
        tui_app_toast(app, "The reminder could not be kept", 1);
        return;
    } else {
        char when[96], note[200];
        when_text(due, when, sizeof(when));
        snprintf(note, sizeof(note), "\xE2\x8F\xB0 Put aside %s. /filter snoozed shows the chats put aside.", when);
        tui_app_toast(app, note, 0);
    }
    tui_app_refresh_tools_prefs(app);
    app->dirty = 1;
}

/* Brings back the chats whose time has come or whose person wrote, and says so. */
void tui_app_reminders_tick(TuiApp *app, int64_t now_ms) {
    ReminderManager *mgr = app->deps.reminders;
    if (!mgr || now_ms - app->reminders_checked_ms < REMINDERS_EVERY_MS || reminder_manager_count(mgr) == 0) return;
    app->reminders_checked_ms = now_ms;
    int count = 0;
    const Chat *chats = tui_app_chat_rows(app, &count);
    int64_t now = (int64_t)time(NULL);
    for (int i = reminder_manager_count(mgr) - 1; i >= 0; i--) {
        const ChatReminder *r = reminder_manager_at(mgr, i);
        if (!r) continue;
        char jid[128], name[128] = "";
        str_copy(jid, sizeof(jid), r->jid);
        int unread = 0, timed = r->due_at > 0;
        for (int c = 0; c < count; c++) {
            if (strcmp(chats[c].jid, jid) != 0) continue;
            unread += chats[c].unread > 0 ? chats[c].unread : 0;
            if (!name[0]) str_copy(name, sizeof(name), chats[c].name);
        }
        if (!reminder_manager_take_due(mgr, jid, unread, now)) continue;
        app->dirty = 1;
        if (unread > 0 || !timed) continue;                 /* their own message brought it back, and says so itself */
        char note[200];
        snprintf(note, sizeof(note), "\xE2\x8F\xB0 Reminder: %.120s", name[0] ? name : jid);
        tui_app_toast(app, note, 0);
        Notification n;
        memset(&n, 0, sizeof(n));
        str_copy(n.chat_jid, sizeof(n.chat_jid), jid);
        str_copy(n.title, sizeof(n.title), name[0] ? name : "tawk");
        str_copy(n.body, sizeof(n.body), "Reminder");
        const Settings *s = settings_manager_current(app->deps.settings);
        if (s->notifications && !s->do_not_disturb && app->deps.notifier) app->deps.notifier->notify(app->deps.notifier, &n);
    }
}

void tui_app_refresh_tools_prefs(TuiApp *app) {
    ContactPanel *panel = &app->contact;
    if (!panel->open) return;
    char labels[160] = "", aside[96] = "";
    if (app->deps.labels) {
        label_manager_of(app->deps.labels, panel->jid, labels, sizeof(labels));
        if (!labels[0]) str_copy(labels, sizeof(labels), "none");
    }
    int64_t due = 0;
    if (app->deps.reminders) {
        if (reminder_manager_snoozed(app->deps.reminders, panel->jid, &due)) when_text(due, aside, sizeof(aside));
        else str_copy(aside, sizeof(aside), "no");
    }
    contact_panel_set_tools_prefs(panel, labels, aside);
    app->dirty = 1;
}
