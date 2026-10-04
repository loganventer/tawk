/* The Agents tab and everything around it: the badge and the one quiet
 * notice when a request arrives, your answers going back through the
 * approval queue, drafts agents leave, and following settings another
 * client changed. The control client runs here too, through the frame
 * hook, without the terminal client knowing what it is. */
#include "tui_app_state.h"
#include "clients/tui/tui_palette.h"
#include "core/settings_schema.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPING_PAUSE_MS   2000     /* a request is mentioned only after this long without a key */
#define LOG_ROWS          400
#define STAMP_WINDOW_MS   60000
#define STAMP_LIMIT       10

static const Settings *settings(TuiApp *app) { return settings_manager_current(app->deps.settings); }

/* A chat's name as the account it is in knows it; the account in view answers for an account that is gone. */
static void chat_name(void *ctx, AccountId account, const char *jid, char *out, size_t size) {
    TuiApp *app = ctx;
    const AccountServices *sv = app->deps.directory ? app->deps.directory->find(app->deps.directory, account) : NULL;
    messaging_manager_display_name(sv ? sv->messaging : app->deps.messaging, jid, out, size);
}

static void account_label(void *ctx, AccountId account, char *out, size_t size) {
    TuiApp *app = ctx;
    out[0] = '\0';
    const AccountServices *sv = app->deps.directory ? app->deps.directory->find(app->deps.directory, account) : NULL;
    if (sv && sv->label && app->deps.directory->count(app->deps.directory) > 1) str_copy(out, size, sv->label);
}

static AutomationEntry *load_log(TuiApp *app, int *count) {
    AutomationEntry *log = NULL;
    *count = 0;
    if (app->deps.automation) automation_manager_recent(app->deps.automation, LOG_ROWS, &log, count);
    return log;
}

static AgentsPanelModel model_of(TuiApp *app, const AutomationEntry *log, int log_count) {
    static const AutomationStatus none;
    static char self_chats[64];
    tui_app_self_chats_summary(app, self_chats, sizeof(self_chats));
    return (AgentsPanelModel){
        app->deps.approvals,
        app->deps.automation ? automation_manager_status(app->deps.automation) : &none,
        log, log_count, settings(app), clock_now_ms(), chat_name, account_label, app, self_chats
    };
}

void tui_app_open_agents(TuiApp *app) {
    agents_panel_open(&app->agents, approval_queue_count(app->deps.approvals) ? AGENTS_VIEW_QUEUE
                                  : settings(app)->control_socket ? AGENTS_VIEW_AGENTS : AGENTS_VIEW_PERMISSIONS);
    app->agents_notice = 0;
    app->dirty = 1;
}

/* Approving many in a row from one agent is a sign of rubber-stamping:
 * suggest allowing it for the session instead. */
static void count_approval(TuiApp *app) {
    int64_t now = clock_now_ms();
    int recent = 0, oldest = 0;
    for (int i = 0; i < 16; i++) {
        if (now - app->agents_approvals[i] < STAMP_WINDOW_MS) recent++;
        if (app->agents_approvals[i] < app->agents_approvals[oldest]) oldest = i;
    }
    app->agents_approvals[oldest] = now;
    if (recent + 1 == STAMP_LIMIT) {
        tui_app_toast(app, "That is a lot of approvals in a minute: s allows the same again for the session, or change Permissions", 0);
    }
}

static void answer(TuiApp *app) {
    AgentsPanel *p = &app->agents;
    char *edited = p->chosen_edited ? agents_panel_edited_text(p) : NULL;
    for (int i = 0; i < p->chosen_count; i++) {
        approval_queue_answer(app->deps.approvals, p->chosen[i], p->chosen_approve, edited, p->chosen_remember);
        if (p->chosen_approve) count_approval(app);
    }
    free(edited);
    const char *what = p->chosen_approve ? (p->chosen_remember ? "Allowed, and for the rest of this session" : "Allowed") : "Declined";
    if (p->chosen_count > 1) {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s %d requests", p->chosen_approve ? "Allowed" : "Declined", p->chosen_count);
        tui_app_toast(app, msg, 0);
    } else {
        tui_app_toast(app, what, 0);
    }
}

static void set_permission(TuiApp *app) {
    const SettingField *f = settings_schema_find(SETTING_CATEGORY_AUTOMATION, app->agents.setting_key);
    if (!f) return;
    Settings s = *settings(app);
    setting_set_from_text(&s, f, app->agents.setting_value);
    tui_app_apply_settings(app, &s);
}

static void handle(TuiApp *app, AgentsPanelRequest request) {
    AutomationManager *am = app->deps.automation;
    switch (request) {
        case AGENTS_REQUEST_APPROVE:
        case AGENTS_REQUEST_DECLINE:    answer(app); break;
        case AGENTS_REQUEST_TOO_LONG:   tui_app_toast(app, "Too long to edit here: decline it and write it yourself", 1); break;
        case AGENTS_REQUEST_DISCONNECT: if (am) automation_manager_command(am, AUTOMATION_COMMAND_DISCONNECT, app->agents.chosen_conn); break;
        case AGENTS_REQUEST_REVOKE:     if (am) automation_manager_command(am, AUTOMATION_COMMAND_REVOKE, app->agents.chosen_conn); break;
        case AGENTS_REQUEST_PAUSE: {
            const AutomationStatus *st = am ? automation_manager_status(am) : NULL;
            int paused = 0;
            for (int i = 0; st && i < st->session_count; i++) if (st->sessions[i].conn == app->agents.chosen_conn) paused = st->sessions[i].paused;
            if (am) automation_manager_command(am, paused ? AUTOMATION_COMMAND_RESUME : AUTOMATION_COMMAND_PAUSE, app->agents.chosen_conn);
            tui_app_toast(app, paused ? "Resumed" : "Paused: its sends and changes are refused until you resume it", 0);
            break;
        }
        case AGENTS_REQUEST_SET_SETTING: set_permission(app); break;
        case AGENTS_REQUEST_SELF_CHATS:                   /* the dialog shows over the chats, and brings you back here */
            app->agents.open = 0;
            tui_app_open_self_chats(app);
            app->self_chats_from_agents = 1;
            break;
        default: break;
    }
    app->dirty = 1;
}

void tui_app_agents_key(TuiApp *app, int is_key, int ch) {
    if (is_key && ch == KEY_F(3)) { app->agents.open = 0; app->dirty = 1; return; }    /* F3 switches back to the chats */
    int count = 0;
    AutomationEntry *log = load_log(app, &count);
    AgentsPanelModel m = model_of(app, log, count);
    handle(app, agents_panel_key(&app->agents, &m, is_key, ch));
    free(log);
    app->dirty = 1;
}

void tui_app_agents_click(TuiApp *app, int y, int x) {
    int count = 0;
    AutomationEntry *log = load_log(app, &count);
    AgentsPanelModel m = model_of(app, log, count);
    handle(app, agents_panel_click(&app->agents, &m, y, x));
    free(log);
    app->dirty = 1;
}

void tui_app_agents_render(TuiApp *app, UiRect area) {
    int count = 0;
    AutomationEntry *log = app->agents.view == AGENTS_VIEW_LOG ? load_log(app, &count) : NULL;
    AgentsPanelModel m = model_of(app, log, count);
    agents_panel_render(&app->agents, area, &m);
    free(log);
}

/* tawk saves settings of its own all the time (the last chat, recent
 * emoji), so only a theme or mouse change made elsewhere is acted on. */
void tui_app_follow_settings(TuiApp *app) {
    unsigned revision = settings_manager_revision(app->deps.settings);
    if (revision == app->settings_revision) return;
    app->settings_revision = revision;
    const Settings *s = settings(app);
    if (strcmp(app->followed_theme, s->theme) != 0) {
        str_copy(app->followed_theme, sizeof(app->followed_theme), s->theme);
        tui_palette_apply(settings_manager_theme(app->deps.settings));
        tui_app_sync_conversation_theme(app, 1);
        clearok(curscr, TRUE);
        app->dirty = 1;
    }
    if (app->followed_mouse != s->mouse) {
        app->followed_mouse = s->mouse;
        mousemask(s->mouse ? (ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION) : 0, NULL);
        app->dirty = 1;
    }
}

/* A draft an agent left: into the input box when its chat is open (below
 * anything you were writing), else kept with that chat. */
static void take_draft(TuiApp *app) {
    char jid[128];
    char *text = messaging_manager_take_offered_draft(app->deps.messaging, jid, sizeof(jid));
    if (!text) return;
    char name[128], msg[256];
    messaging_manager_display_name(app->deps.messaging, jid, name, sizeof(name));
    if (strcmp(jid, messaging_manager_open_jid(app->deps.messaging)) == 0) {
        char *mine = composer_view_text(&app->composer);
        if (mine && *mine) {
            size_t n = strlen(mine) + strlen(text) + 3;
            char *both = malloc(n);
            if (both) {
                snprintf(both, n, "%s\n\n%s", mine, text);
                composer_view_set_text(&app->composer, both);
                free(both);
            }
            snprintf(msg, sizeof(msg), "\xF0\x9F\xA4\x96 An agent added a draft below yours; edit it and send it when you are ready");
        } else {
            composer_view_set_text(&app->composer, text);
            snprintf(msg, sizeof(msg), "\xF0\x9F\xA4\x96 An agent wrote a draft; edit it and send it when you are ready");
        }
        free(mine);
    } else {
        snprintf(msg, sizeof(msg), "\xF0\x9F\xA4\x96 An agent left a draft for %s", name);
    }
    free(text);
    tui_app_toast(app, msg, 0);
}

/* New requests never take focus: the badge changes at once, and a single
 * line says so once you have stopped typing. */
static void notice_requests(TuiApp *app) {
    uint64_t arrivals = approval_queue_arrivals(app->deps.approvals);
    if (arrivals != app->agents_seen) {
        app->agents_seen = arrivals;
        app->agents_notice = !app->agents.open;
        app->dirty = 1;
    }
    if (!app->agents_notice || app->agents.open) return;
    if (clock_now_ms() - app->last_key_ms < TYPING_PAUSE_MS) return;
    int n = approval_queue_count(app->deps.approvals);
    app->agents_notice = 0;
    if (n == 0) return;
    const ApprovalRequest *last = approval_queue_at(app->deps.approvals, n - 1);
    char msg[480];
    snprintf(msg, sizeof(msg), "\xF0\x9F\xA4\x96 %s asks to %s%s%s%s%s%s: F3 to answer", last->client, last->action,
             last->chat_name[0] ? " in " : "", last->chat_name,
             last->account_label[0] ? " (" : "", last->account_label, last->account_label[0] ? ")" : "");
    tui_app_toast(app, msg, last->risk == APPROVAL_RISK_HIGH);
}

void tui_app_agents_tick(TuiApp *app) {
    if (app->deps.frame_hook && app->deps.frame_hook->tick(app->deps.frame_hook)) app->dirty = 1;
    char did[256];
    while (app->deps.automation && automation_manager_take_notice(app->deps.automation, did, sizeof(did))) tui_app_toast(app, did, 0);
    if (app->deps.automation && automation_manager_take_changed(app->deps.automation)) app->dirty = 1;
    if (app->agents.open) app->dirty = 1;                  /* the countdowns */
    tui_app_follow_settings(app);
    if (app->deps.approvals) notice_requests(app);
    take_draft(app);
}
