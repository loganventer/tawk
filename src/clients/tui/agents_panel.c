#include "clients/tui/agents_panel.h"
#include "core/account.h"
#include "clients/tui/toggle_switch.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "core/settings_schema.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"
#include "utilities/utf8_text.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DOT       " \xC2\xB7 "
#define MAX_LOG   512
#define EDIT_MAX  TEXT_FIELD_CAPACITY

/* Where a request would act: the chat, and the account when agents may use more than one. */
static void request_place(const ApprovalRequest *r, char *out, size_t size) {
    if (r->account_label[0] && r->chat_name[0]) snprintf(out, size, "%s (%s)", r->chat_name, r->account_label);
    else if (r->account_label[0]) snprintf(out, size, "(%s)", r->account_label);
    else str_copy(out, size, r->chat_name);
}

static const char *const TAB_NAMES[AGENTS_VIEW_COUNT] = { "Queue", "Agents", "Log", "Permissions" };
static const char *const PERMISSION_KEYS[] = { "control_socket", "access", "chats", "confirm_cli", "writes_per_minute", "ai_disclaimer", "ai_disclaimer_text", "push_received", "push_sent", "push_read", "push_reactions", "push_edits", "push_scheduled", "push_presence", "self_approval_chats", "self_approvals_per_hour" };
#define AGENT_ROWS 2   /* lines an agent takes in the Agents list */
#define PERMISSION_COUNT ((int)(sizeof(PERMISSION_KEYS) / sizeof(PERMISSION_KEYS[0])))
static const char *const FILTER_NAMES[] = { "everything", "allowed", "declined or expired", "refused or failed" };

void agents_panel_open(AgentsPanel *p, AgentsView view) {
    int reads = p->log_reads;
    memset(p, 0, sizeof(*p));
    p->log_reads = reads;
    p->view = view;
    text_field_init(&p->edit, EDIT_MAX);
    text_field_allow_newlines(&p->edit, 1);
    text_field_init(&p->search, 60);
    p->open = 1;
}

/* ---- what each view lists ------------------------------------------------ */

static int log_matches(const AgentsPanel *p, const AutomationEntry *e) {
    if (e->outcome == AUTOMATION_OUTCOME_READ && !p->log_reads) return 0;
    switch (p->log_filter) {
        case 1: if (e->outcome != AUTOMATION_OUTCOME_DONE && e->outcome != AUTOMATION_OUTCOME_APPROVED && e->outcome != AUTOMATION_OUTCOME_ALLOWED && e->outcome != AUTOMATION_OUTCOME_SELF_APPROVED) return 0; break;
        case 2: if (e->outcome != AUTOMATION_OUTCOME_DECLINED && e->outcome != AUTOMATION_OUTCOME_TIMED_OUT) return 0; break;
        case 3: if (e->outcome != AUTOMATION_OUTCOME_REFUSED && e->outcome != AUTOMATION_OUTCOME_FAILED && e->outcome != AUTOMATION_OUTCOME_RATE_LIMITED) return 0; break;
        default: break;
    }
    char *q = text_field_text(&p->search);
    int ok = 1;
    if (q && *q) ok = strcasestr(e->summary, q) || strcasestr(e->client, q) || strcasestr(e->op, q) || strcasestr(e->chat_jid, q);
    free(q);
    return ok;
}

static int log_rows(const AgentsPanel *p, const AgentsPanelModel *m, int *out, int max) {
    int n = 0;
    for (int i = 0; i < m->log_count && n < max; i++) if (log_matches(p, &m->log[i])) out[n++] = i;
    return n;
}

static int row_count(const AgentsPanel *p, const AgentsPanelModel *m) {
    int rows[MAX_LOG];
    switch (p->view) {
        case AGENTS_VIEW_QUEUE:       return approval_queue_count(m->queue);
        case AGENTS_VIEW_AGENTS:      return m->status ? m->status->session_count : 0;
        case AGENTS_VIEW_LOG:         return log_rows(p, m, rows, MAX_LOG);
        case AGENTS_VIEW_PERMISSIONS: return PERMISSION_COUNT;
        default:                      return 0;
    }
}

static const ApprovalRequest *selected_request(const AgentsPanel *p, const AgentsPanelModel *m) {
    return approval_queue_at(m->queue, p->selected[AGENTS_VIEW_QUEUE]);
}

static const SettingField *permission_field(int row) {
    return row >= 0 && row < PERMISSION_COUNT ? settings_schema_find(SETTING_CATEGORY_AUTOMATION, PERMISSION_KEYS[row]) : NULL;
}

static void clamp(AgentsPanel *p, const AgentsPanelModel *m) {
    int n = row_count(p, m);
    int *sel = &p->selected[p->view];
    if (*sel >= n) *sel = n - 1;
    if (*sel < 0) *sel = 0;
}

/* ---- marking and choosing ------------------------------------------------ */

static int marked(const AgentsPanel *p, int id) {
    for (int i = 0; i < p->mark_count; i++) if (p->marks[i] == id) return 1;
    return 0;
}

static void toggle_mark(AgentsPanel *p, const ApprovalRequest *r) {
    for (int i = 0; i < p->mark_count; i++) {
        if (p->marks[i] == r->id) { p->marks[i] = p->marks[--p->mark_count]; return; }
    }
    if (r->risk == APPROVAL_RISK_HIGH) { str_copy(p->hint, sizeof(p->hint), "HIGH requests are answered one at a time"); return; }
    if (p->mark_count < APPROVAL_QUEUE_MAX) p->marks[p->mark_count++] = r->id;
}

/* Answers the marked requests that are still waiting, or else the selected one. */
static AgentsPanelRequest choose(AgentsPanel *p, const AgentsPanelModel *m, int approve, int remember) {
    p->chosen_count = 0;
    p->chosen_approve = approve;
    p->chosen_remember = remember;
    p->chosen_edited = 0;
    for (int i = 0; i < p->mark_count; i++) {
        const ApprovalRequest *r = approval_queue_find(m->queue, p->marks[i]);
        if (r && (r->risk != APPROVAL_RISK_HIGH || !approve)) p->chosen[p->chosen_count++] = r->id;
    }
    p->mark_count = 0;
    if (p->chosen_count == 0) {
        const ApprovalRequest *r = selected_request(p, m);
        if (!r) return AGENTS_REQUEST_NONE;
        p->chosen[p->chosen_count++] = r->id;
    }
    return approve ? AGENTS_REQUEST_APPROVE : AGENTS_REQUEST_DECLINE;
}

/* ---- keys ----------------------------------------------------------------- */

static int is_enter(int is_key, int ch) { return (!is_key && (ch == '\n' || ch == '\r')) || (is_key && ch == KEY_ENTER); }

static AgentsPanelRequest editing_key(AgentsPanel *p, const AgentsPanelModel *m, int is_key, int ch) {
    if (!is_key && ch == 27) { p->editing = 0; return AGENTS_REQUEST_REDRAW; }
    if (is_enter(is_key, ch)) {
        const ApprovalRequest *r = selected_request(p, m);
        p->editing = 0;
        if (!r) return AGENTS_REQUEST_REDRAW;
        p->chosen[0] = r->id;
        p->chosen_count = 1;
        p->chosen_approve = 1;
        p->chosen_remember = 0;
        p->chosen_edited = 1;
        return AGENTS_REQUEST_APPROVE;
    }
    text_field_key(&p->edit, is_key, ch);
    return AGENTS_REQUEST_REDRAW;
}

static AgentsPanelRequest confirming_key(AgentsPanel *p, const AgentsPanelModel *m, int is_key, int ch) {
    int id = p->confirming;
    if ((!is_key && (ch == '\t' || ch == 'h' || ch == 'l')) || (is_key && (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_BTAB))) {
        p->confirm_on_action = !p->confirm_on_action;
        return AGENTS_REQUEST_REDRAW;
    }
    int yes = (!is_key && ch == 'Y') || (is_enter(is_key, ch) && p->confirm_on_action);
    p->confirming = 0;
    p->confirm_on_action = 0;
    if (!yes || !approval_queue_find(m->queue, id)) {
        str_copy(p->hint, sizeof(p->hint), "Kept: nothing was done. d declines it, or leave it to expire");
        return AGENTS_REQUEST_REDRAW;
    }
    p->chosen[0] = id;
    p->chosen_count = 1;
    p->chosen_approve = 1;
    p->chosen_remember = 0;
    p->chosen_edited = 0;
    return AGENTS_REQUEST_APPROVE;
}

static AgentsPanelRequest queue_key(AgentsPanel *p, const AgentsPanelModel *m, int is_key, int ch) {
    const ApprovalRequest *r = selected_request(p, m);
    if (is_key || !r) return AGENTS_REQUEST_NONE;
    switch (ch) {
        case ' ': toggle_mark(p, r); return AGENTS_REQUEST_REDRAW;
        case 'a':
            if (p->mark_count == 0 && r->risk == APPROVAL_RISK_HIGH) {
                str_copy(p->hint, sizeof(p->hint), "HIGH requests need Shift+A, then Y");
                return AGENTS_REQUEST_REDRAW;
            }
            return choose(p, m, 1, 0);
        case 'A':
            if (r->risk == APPROVAL_RISK_HIGH) { p->confirming = r->id; p->confirm_on_action = 0; return AGENTS_REQUEST_REDRAW; }
            return choose(p, m, 1, 0);
        case 's':
            if (r->risk == APPROVAL_RISK_HIGH) {
                str_copy(p->hint, sizeof(p->hint), "HIGH requests are never allowed for a whole session");
                return AGENTS_REQUEST_REDRAW;
            }
            p->mark_count = 0;
            return choose(p, m, 1, 1);
        case 'd': return choose(p, m, 0, 0);
        case 'e':
            if (!r->editable) { str_copy(p->hint, sizeof(p->hint), "This request has no text to edit"); return AGENTS_REQUEST_REDRAW; }
            if (r->text && utf8_columns(r->text) > EDIT_MAX) return AGENTS_REQUEST_TOO_LONG;
            text_field_set(&p->edit, r->text ? r->text : "");
            p->editing = 1;
            return AGENTS_REQUEST_REDRAW;
        default: return AGENTS_REQUEST_NONE;
    }
}

static AgentsPanelRequest agents_key(AgentsPanel *p, const AgentsPanelModel *m, int is_key, int ch) {
    int row = p->selected[AGENTS_VIEW_AGENTS];
    if (is_key || !m->status || row >= m->status->session_count) return AGENTS_REQUEST_NONE;
    p->chosen_conn = m->status->sessions[row].conn;
    switch (ch) {
        case 'x': return AGENTS_REQUEST_DISCONNECT;
        case 'p': return AGENTS_REQUEST_PAUSE;
        case 'r': return AGENTS_REQUEST_REVOKE;
        default:  return AGENTS_REQUEST_NONE;
    }
}

static AgentsPanelRequest log_key(AgentsPanel *p, int is_key, int ch) {
    if (p->searching) {
        if ((!is_key && ch == 27) || is_enter(is_key, ch)) { p->searching = 0; return AGENTS_REQUEST_REDRAW; }
        text_field_key(&p->search, is_key, ch);
        p->selected[AGENTS_VIEW_LOG] = 0;
        return AGENTS_REQUEST_REDRAW;
    }
    if (is_key) return AGENTS_REQUEST_NONE;
    switch (ch) {
        case 'r': p->log_reads = !p->log_reads; p->selected[AGENTS_VIEW_LOG] = 0; return AGENTS_REQUEST_REDRAW;
        case 'f': p->log_filter = (p->log_filter + 1) % 4; p->selected[AGENTS_VIEW_LOG] = 0; return AGENTS_REQUEST_REDRAW;
        case '/': p->searching = 1; return AGENTS_REQUEST_REDRAW;
        default:  return AGENTS_REQUEST_NONE;
    }
}

/* Enter or Space changes a permission: switches flip, choices move on, and
 * text or numbers are typed. Left and Right step a number. */
static AgentsPanelRequest permissions_key(AgentsPanel *p, const AgentsPanelModel *m, int is_key, int ch) {
    const SettingField *f = permission_field(p->selected[AGENTS_VIEW_PERMISSIONS]);
    if (!f) return AGENTS_REQUEST_NONE;
    if (p->editing_setting) {
        if (!is_key && ch == 27) { p->editing_setting = 0; return AGENTS_REQUEST_REDRAW; }
        if (!is_enter(is_key, ch)) { text_field_key(&p->edit, is_key, ch); return AGENTS_REQUEST_REDRAW; }
        p->editing_setting = 0;
        char *text = text_field_text(&p->edit);
        str_copy(p->setting_value, sizeof(p->setting_value), text ? text : "");
        free(text);
        str_copy(p->setting_key, sizeof(p->setting_key), f->key);
        return AGENTS_REQUEST_SET_SETTING;
    }
    char value[600];
    setting_to_text(m->settings, f, value, sizeof(value));
    str_copy(p->setting_key, sizeof(p->setting_key), f->key);
    int change = (!is_key && ch == ' ') || is_enter(is_key, ch);
    if (!strcmp(f->key, "self_approval_chats")) return change ? AGENTS_REQUEST_SELF_CHATS : AGENTS_REQUEST_NONE;   /* chosen with a switch per chat */
    if (f->kind == SETTING_KIND_BOOL && change) {
        str_copy(p->setting_value, sizeof(p->setting_value), setting_get_int(m->settings, f) ? "off" : "on");
        return AGENTS_REQUEST_SET_SETTING;
    }
    if (f->kind == SETTING_KIND_CHOICE && change) {
        const char *at = strstr(f->choices, value);
        const char *next = at ? strchr(at, '|') : NULL;
        const char *start = next ? next + 1 : f->choices;
        size_t len = strcspn(start, "|");
        snprintf(p->setting_value, sizeof(p->setting_value), "%.*s", (int)len, start);
        return AGENTS_REQUEST_SET_SETTING;
    }
    if (f->kind == SETTING_KIND_INT && is_key && (ch == KEY_LEFT || ch == KEY_RIGHT)) {
        snprintf(p->setting_value, sizeof(p->setting_value), "%d", setting_get_int(m->settings, f) + (ch == KEY_RIGHT ? f->step : -f->step));
        return AGENTS_REQUEST_SET_SETTING;
    }
    if ((f->kind == SETTING_KIND_STRING || f->kind == SETTING_KIND_INT) && change) {
        text_field_set(&p->edit, value);
        p->editing_setting = 1;
        return AGENTS_REQUEST_REDRAW;
    }
    return AGENTS_REQUEST_NONE;
}

AgentsPanelRequest agents_panel_key(AgentsPanel *p, const AgentsPanelModel *m, int is_key, int ch) {
    if (p->confirming) return confirming_key(p, m, is_key, ch);
    if (p->editing) return editing_key(p, m, is_key, ch);
    if (p->editing_setting || (p->searching && p->view == AGENTS_VIEW_LOG)) {
        return p->view == AGENTS_VIEW_LOG ? log_key(p, is_key, ch) : permissions_key(p, m, is_key, ch);
    }
    p->hint[0] = '\0';
    if (!is_key && (ch == 27 || ch == 'q')) { p->open = 0; return AGENTS_REQUEST_CLOSE; }
    if (!is_key && ch >= '1' && ch < '1' + AGENTS_VIEW_COUNT) { p->view = (AgentsView)(ch - '1'); clamp(p, m); return AGENTS_REQUEST_REDRAW; }
    if ((!is_key && ch == '\t') || (is_key && ch == KEY_BTAB)) {
        int step = is_key ? AGENTS_VIEW_COUNT - 1 : 1;
        p->view = (AgentsView)((p->view + step) % AGENTS_VIEW_COUNT);
        clamp(p, m);
        return AGENTS_REQUEST_REDRAW;
    }
    int *sel = &p->selected[p->view];
    if ((is_key && ch == KEY_DOWN) || (!is_key && ch == 'j')) { (*sel)++; clamp(p, m); return AGENTS_REQUEST_REDRAW; }
    if ((is_key && ch == KEY_UP) || (!is_key && ch == 'k')) { (*sel)--; clamp(p, m); return AGENTS_REQUEST_REDRAW; }
    if (is_key && ch == KEY_HOME) { *sel = 0; return AGENTS_REQUEST_REDRAW; }
    if (is_key && ch == KEY_END) { *sel = row_count(p, m) - 1; clamp(p, m); return AGENTS_REQUEST_REDRAW; }
    if (is_enter(is_key, ch) && p->view != AGENTS_VIEW_PERMISSIONS) { p->expanded = !p->expanded; return AGENTS_REQUEST_REDRAW; }
    switch (p->view) {
        case AGENTS_VIEW_QUEUE:       return queue_key(p, m, is_key, ch);
        case AGENTS_VIEW_AGENTS:      return agents_key(p, m, is_key, ch);
        case AGENTS_VIEW_LOG:         return log_key(p, is_key, ch);
        case AGENTS_VIEW_PERMISSIONS: return permissions_key(p, m, is_key, ch);
        default:                      return AGENTS_REQUEST_NONE;
    }
}

AgentsPanelRequest agents_panel_click(AgentsPanel *p, const AgentsPanelModel *m, int y, int x) {
    if (p->confirming || p->editing) return AGENTS_REQUEST_NONE;
    if (!ui_rect_contains(p->last_rect, y, x)) return AGENTS_REQUEST_NONE;
    for (int i = 0; i < AGENTS_VIEW_COUNT; i++) {
        if (ui_rect_contains(p->tab_rects[i], y, x)) { p->view = (AgentsView)i; clamp(p, m); return AGENTS_REQUEST_REDRAW; }
    }
    if (ui_rect_contains(p->list_rect, y, x)) {
        /* An agent takes two lines of the list; a click on either chooses it. */
        p->selected[p->view] = p->scroll[p->view] + (y - p->list_rect.y) / (p->view == AGENTS_VIEW_AGENTS ? AGENT_ROWS : 1);
        clamp(p, m);
        return AGENTS_REQUEST_REDRAW;
    }
    return AGENTS_REQUEST_NONE;
}

void agents_panel_paste(AgentsPanel *p, const char *utf8) {
    if (p->editing || p->editing_setting) text_field_paste(&p->edit, utf8);
    else if (p->searching) text_field_paste(&p->search, utf8);
}

char *agents_panel_edited_text(const AgentsPanel *p) { return text_field_text(&p->edit); }

/* ---- drawing -------------------------------------------------------------- */

static int risk_attr(ApprovalRisk r) {
    if (r == APPROVAL_RISK_HIGH) return tui_palette_attr(THEME_SLOT_WARN) | ATTR_BOLD;
    if (r == APPROVAL_RISK_MEDIUM) return tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD;
    return tui_palette_attr(THEME_SLOT_DIM);
}

/* `s` in exactly `cols` columns: cut with "…" when longer, padded with
 * spaces when shorter. printf's widths count bytes, which misaligns the
 * columns after an emoji, a "·" or an accented name. Returns `out`. */
static char *column(char *out, size_t size, const char *s, int cols) {
    int used = 0;
    size_t len = strlen(s), n = utf8_fit(s, len, cols, &used);
    if (n < len) n = utf8_fit(s, len, cols - 1, &used);
    int w = snprintf(out, size, "%.*s%s", (int)n, s, n < len ? "\xE2\x80\xA6" : "");
    if (n < len) used++;
    while (used < cols && w >= 0 && (size_t)w + 1 < size) { out[w++] = ' '; out[w] = '\0'; used++; }
    return out;
}

static void minutes(int64_t ms, char *out, size_t size) {
    if (ms < 0) ms = 0;
    int64_t s = ms / 1000;
    snprintf(out, size, "%lld:%02lld", (long long)(s / 60), (long long)(s % 60));
}

static void draw_tabs(AgentsPanel *p, const AgentsPanelModel *m, int y, UiRect box, int base) {
    int x = box.x + 2;
    for (int i = 0; i < AGENTS_VIEW_COUNT; i++) {
        char label[48];
        if (i == AGENTS_VIEW_QUEUE) snprintf(label, sizeof(label), " %d %s (%d) ", i + 1, TAB_NAMES[i], approval_queue_count(m->queue));
        else if (i == AGENTS_VIEW_AGENTS) snprintf(label, sizeof(label), " %d %s (%d) ", i + 1, TAB_NAMES[i], m->status ? m->status->session_count : 0);
        else snprintf(label, sizeof(label), " %d %s ", i + 1, TAB_NAMES[i]);
        int attr = i == (int)p->view ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) | ATTR_BOLD : base;
        int used = tui_text(y, x, box.x + box.w - 2 - x, label, attr);
        p->tab_rects[i] = (UiRect){ y, x, 1, used };
        x += used + 1;
    }
}

static void keep_in_view(AgentsPanel *p, int rows) {
    int *sel = &p->selected[p->view], *top = &p->scroll[p->view];
    if (*sel < *top) *top = *sel;
    if (rows > 0 && *sel >= *top + rows) *top = *sel - rows + 1;
    if (*top < 0) *top = 0;
}

/* Wrapped text inside `r`, from its first line; returns the lines used. */
static int draw_wrapped(UiRect r, const char *text, int attr) {
    if (!text || r.h <= 0) return 0;
    TextLine *lines = NULL;
    int n = utf8_wrap(text, r.w, &lines), shown = 0;
    for (int i = 0; i < n && shown < r.h; i++, shown++) {
        tui_text_n(r.y + shown, r.x, r.w, text + lines[i].offset, lines[i].length, attr);
    }
    if (n > r.h && r.h > 0) tui_text_right(r.y + r.h - 1, r.x + r.w, 12, " \xE2\x80\xA6more ", attr | ATTR_REVERSE);
    free(lines);
    return shown;
}

static void draw_queue(AgentsPanel *p, const AgentsPanelModel *m, UiRect list, UiRect detail, int base) {
    int n = approval_queue_count(m->queue);
    if (n == 0) {
        tui_text(list.y, list.x, list.w, "Nothing is waiting. Reads never need you; sends and changes wait here for your answer.", base | ATTR_DIM);
        if (m->status && !m->status->listening && list.h > 1) {
            tui_text(list.y + 1, list.x, list.w, "Agent access is off, so nothing can connect: 4 Permissions turns it on.", base | ATTR_DIM);
        }
        return;
    }
    keep_in_view(p, list.h);
    for (int row = 0; row < list.h && p->scroll[0] + row < n; row++) {
        int i = p->scroll[0] + row;
        const ApprovalRequest *r = approval_queue_at(m->queue, i);
        int sel = i == p->selected[0];
        int attr = sel ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) : base;
        tui_fill((UiRect){ list.y + row, list.x, 1, list.w }, attr);
        char tag[24], mark[16], age[32], left[32], line[400], who[64], what[160], where[160];
        snprintf(tag, sizeof(tag), "%s%s %-4s", marked(p, r->id) ? "\xE2\x9C\x93" : " ",
                 column(mark, sizeof(mark), approval_risk_mark(r->risk), 2), approval_risk_label(r->risk));
        int x = list.x + tui_text(list.y + row, list.x, 10, tag, sel ? attr | ATTR_BOLD : risk_attr(r->risk));
        minutes(m->now_ms - r->asked_ms, age, sizeof(age));
        minutes(r->expires_ms - m->now_ms, left, sizeof(left));
        char place[200];
        request_place(r, place, sizeof(place));
        snprintf(line, sizeof(line), " %s %s %s", column(who, sizeof(who), r->client, 12),
                 column(what, sizeof(what), r->action, 30), column(where, sizeof(where), place, 24));
        tui_text(list.y + row, x, list.w - 24 - (x - list.x), line, attr);
        char times[96];
        snprintf(times, sizeof(times), "%s ago  %s left ", age, left);
        int urgent = r->expires_ms - m->now_ms < 30000;
        tui_text_right(list.y + row, list.x + list.w, 24, times, urgent ? tui_palette_attr(THEME_SLOT_WARN) | (sel ? ATTR_REVERSE : 0) : attr);
    }
    const ApprovalRequest *r = selected_request(p, m);
    if (!r || detail.h <= 0) return;
    char head[600], place[200];
    request_place(r, place, sizeof(place));
    snprintf(head, sizeof(head), "%s %s" DOT "%s (%s)" DOT "wants to %s%s%s", approval_risk_mark(r->risk), approval_risk_label(r->risk),
             r->client, r->origin == CONTROL_ORIGIN_MCP ? "acting for a model" : "your shell", r->action,
             place[0] ? " in " : "", place);
    tui_text(detail.y, detail.x, detail.w, head, risk_attr(r->risk));
    UiRect body = { detail.y + 1, detail.x + 2, detail.h - 1, detail.w - 2 };
    if (p->editing) {
        text_field_render(&p->edit, body, tui_palette_attr(THEME_SLOT_COMPOSER), 1, &p->caret);
    } else if (r->text) {
        draw_wrapped(body, r->text, base);
    }
}

static void draw_agents(AgentsPanel *p, const AgentsPanelModel *m, UiRect list, UiRect detail, int base) {
    const AutomationStatus *st = m->status;
    if (!st || st->session_count == 0) {
        tui_text(list.y, list.x, list.w, st && st->listening ? "No agent is connected." :
                 "The control socket is off. Turn it on in 4 Permissions or Settings > Automation.", base | ATTR_DIM);
        return;
    }
    /* Each agent takes two lines: who it is, and under that what it says it is doing. */
    int slots = list.h / AGENT_ROWS > 0 ? list.h / AGENT_ROWS : 1;
    keep_in_view(p, slots);
    for (int slot = 0; slot < slots && p->scroll[1] + slot < st->session_count; slot++) {
        int i = p->scroll[1] + slot, y = list.y + slot * AGENT_ROWS;
        const AutomationSession *a = &st->sessions[i];
        int attr = i == p->selected[1] ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) : base;
        tui_fill((UiRect){ y, list.x, AGENT_ROWS <= list.h ? AGENT_ROWS : 1, list.w }, attr);
        char since[32], line[640], who[64], label[128];
        clock_format_short(a->since, m->settings->use_24h_clock, since, sizeof(since));
        snprintf(line, sizeof(line), "%s %s %s %-18s since %-8s %4d requests  %d allowed for the session%s",
                 a->origin == CONTROL_ORIGIN_MCP ? "\xF0\x9F\xA4\x96" : "\xE2\x8C\xA8",
                 column(who, sizeof(who), a->client, 9), column(label, sizeof(label), a->label, 30),
                 a->origin == CONTROL_ORIGIN_MCP ? "acting for a model" : "your shell", since, a->requests, a->allowances,
                 a->paused ? "  PAUSED" : "");
        tui_text(y, list.x, list.w, line, attr);
        if (y + 1 < list.y + list.h) {
            /* The agent's own words: a claim about what it is doing, not a fact. */
            char doing[200];
            snprintf(doing, sizeof(doing), "   %s", a->doing[0] ? a->doing :
                     a->origin == CONTROL_ORIGIN_MCP ? "has not said what it is doing" : "a command you ran");
            tui_text(y + 1, list.x, list.w, doing, i == p->selected[1] ? attr : base | ATTR_DIM);
        }
    }
    if (detail.h > 0) {
        char where[600];
        snprintf(where, sizeof(where), "Listening on %s. Every request acts as you, on this computer's WhatsApp.", st->socket_path);
        draw_wrapped(detail, where, base | ATTR_DIM);
    }
}

static void draw_log(AgentsPanel *p, const AgentsPanelModel *m, UiRect list, UiRect detail, int base) {
    int rows[MAX_LOG];
    int n = log_rows(p, m, rows, MAX_LOG);
    char status[200];
    char *q = text_field_text(&p->search);
    snprintf(status, sizeof(status), "Showing %s%s%s%s", FILTER_NAMES[p->log_filter], p->log_reads ? ", reads included" : "",
             q && *q ? ", matching " : "", q && *q ? q : "");
    free(q);
    if (p->searching) {
        tui_text(list.y, list.x, 9, "Search: ", base | ATTR_BOLD);
        text_field_render(&p->search, (UiRect){ list.y, list.x + 8, 1, list.w - 8 }, tui_palette_attr(THEME_SLOT_COMPOSER), 1, &p->caret);
    } else {
        tui_text(list.y, list.x, list.w, status, base | ATTR_DIM);
    }
    list.y++;
    list.h--;
    if (n == 0) { tui_text(list.y, list.x, list.w, "Nothing matches.", base | ATTR_DIM); return; }
    keep_in_view(p, list.h);
    for (int row = 0; row < list.h && p->scroll[2] + row < n; row++) {
        int i = p->scroll[2] + row;
        const AutomationEntry *e = &m->log[rows[i]];
        int attr = i == p->selected[2] ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) : base;
        tui_fill((UiRect){ list.y + row, list.x, 1, list.w }, attr);
        char when[32], who[128] = "", line[600];
        clock_format_short(e->at, m->settings->use_24h_clock, when, sizeof(when));
        if (e->chat_jid[0] && m->name_of) m->name_of(m->ctx, e->account, e->chat_jid, who, sizeof(who));
        char label[ACCOUNT_LABEL_SIZE] = "";
        if (m->label_of) m->label_of(m->ctx, e->account, label, sizeof(label));
        if (label[0]) {                                     /* which of your numbers it was about */
            char both[128];
            snprintf(both, sizeof(both), "%.60s (%.40s)", who, label);
            str_copy(who, sizeof(who), both);
        }
        char client[64], chat[160];
        snprintf(line, sizeof(line), "%-10s %s %-16.16s %s %-23.23s %s", when, column(client, sizeof(client), e->client, 12),
                 e->op, column(chat, sizeof(chat), who, label[0] ? 24 : 16), automation_outcome_name(e->outcome), e->summary);
        int bad = e->outcome == AUTOMATION_OUTCOME_DECLINED || e->outcome == AUTOMATION_OUTCOME_REFUSED || e->outcome == AUTOMATION_OUTCOME_FAILED;
        tui_text(list.y + row, list.x, list.w, line, bad && i != p->selected[2] ? attr | ATTR_DIM : attr);
    }
    int at = p->selected[2];
    if (detail.h > 0 && at >= 0 && at < n) draw_wrapped(detail, m->log[rows[at]].summary, base);
}

static void draw_permissions(AgentsPanel *p, const AgentsPanelModel *m, UiRect list, UiRect detail, int base) {
    keep_in_view(p, list.h);
    for (int row = 0; row < list.h && p->scroll[3] + row < PERMISSION_COUNT; row++) {
        int i = p->scroll[3] + row;
        const SettingField *f = permission_field(i);
        if (!f) continue;
        int sel = i == p->selected[3];
        int attr = sel ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) : base;
        tui_fill((UiRect){ list.y + row, list.x, 1, list.w }, attr);
        char value[600], line[800];
        setting_to_text(m->settings, f, value, sizeof(value));
        if (f->kind == SETTING_KIND_BOOL) str_copy(value, sizeof(value), toggle_switch_text(setting_get_int(m->settings, f)));
        if (!strcmp(f->key, "chats") && !value[0]) str_copy(value, sizeof(value), "(every chat except locked ones)");
        if (!strcmp(f->key, "self_approval_chats")) str_copy(value, sizeof(value), m->self_chats ? m->self_chats : "");
        snprintf(line, sizeof(line), "%-33s %s", f->label, value);
        if (sel && p->editing_setting) {
            tui_text(list.y + row, list.x, 34, f->label, attr);
            text_field_render(&p->edit, (UiRect){ list.y + row, list.x + 34, 1, list.w - 34 }, tui_palette_attr(THEME_SLOT_COMPOSER), 1, &p->caret);
        } else {
            tui_text(list.y + row, list.x, list.w, line, attr);
        }
    }
    const SettingField *f = permission_field(p->selected[3]);
    UiRect r = detail;
    if (f && r.h > 0) { int used = draw_wrapped(r, f->help, base); r.y += used + 1; r.h -= used + 1; }
    static const char *const NOTES =
        "Needs: tawk running with the control socket on, and tawk-mcp (or a tawk command) connecting to it. "
        "Risks: chat text an agent reads goes to its model's provider; a message someone sends you can try to steer the agent; "
        "an allowed send goes out as you. Guards: sends and changes wait here for you, deletes and blocks need a yes in "
        "the agent's app and a Y here, locked chats are never shown, and everything is logged.";
    if (r.h > 0) draw_wrapped(r, NOTES, base | ATTR_DIM);
    if (m->status && m->status->error[0] && detail.h > 1) tui_text(detail.y + detail.h - 1, detail.x, detail.w, m->status->error, tui_palette_attr(THEME_SLOT_WARN));
}

/* A HIGH request asks once more, in a box unlike the rest so it is read. */
static void draw_confirm(AgentsPanel *p, const AgentsPanelModel *m, UiRect box) {
    const ApprovalRequest *r = approval_queue_find(m->queue, p->confirming);
    if (!r) return;
    int w = box.w < 64 ? box.w - 2 : 64, h = 9;
    UiRect c = { box.y + (box.h - h) / 2, box.x + (box.w - w) / 2, h, w };
    int warn = tui_palette_attr(THEME_SLOT_WARN) | ATTR_BOLD;
    tui_fill(c, tui_palette_attr(THEME_SLOT_BASE));
    tui_box(c, " !! HIGH risk ", warn);
    char line[520];
    char place[200];
    request_place(r, place, sizeof(place));
    snprintf(line, sizeof(line), "%s wants to %s%s%s.", r->client, r->action, place[0] ? ": " : "", place);
    draw_wrapped((UiRect){ c.y + 2, c.x + 3, 3, c.w - 6 }, line, warn);
    tui_text(c.y + 5, c.x + 3, c.w - 6, "This cannot be undone. Press Y to allow it.", tui_palette_attr(THEME_SLOT_WARN));   /* on the box's colours */
    const char *keep = "  Keep  ", *act = "  Allow  ";
    int bx = c.x + (c.w - 20) / 2;
    tui_text(c.y + 7, bx, 8, keep, p->confirm_on_action ? tui_palette_attr(THEME_SLOT_BASE) : tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) | ATTR_BOLD);
    tui_text(c.y + 7, bx + 11, 9, act, p->confirm_on_action ? warn | ATTR_REVERSE : warn);
}

static const char *keys_for(const AgentsPanel *p) {
    if (p->confirming) return " Y allow" DOT "Esc keep ";
    if (p->editing) return " Enter approve as edited" DOT "Shift+Enter new line" DOT "Esc stop editing ";
    if (p->editing_setting) return " Enter save" DOT "Esc cancel ";
    switch (p->view) {
        case AGENTS_VIEW_QUEUE: return " a approve" DOT "e edit" DOT "s allow for session" DOT "d decline" DOT "Space mark" DOT "Shift+A HIGH" DOT "Esc close ";
        case AGENTS_VIEW_AGENTS: return " x disconnect" DOT "p pause or resume" DOT "r forget its allowances" DOT "Esc close ";
        case AGENTS_VIEW_LOG: return " f filter" DOT "r show reads" DOT "/ search" DOT "Enter more" DOT "Esc close ";
        default: return " Enter change" DOT "\xE2\x86\x90\xE2\x86\x92 step a number" DOT "Esc close ";
    }
}

void agents_panel_render(AgentsPanel *p, UiRect area, const AgentsPanelModel *m) {
    UiRect box = area;
    p->last_rect = box;
    int base = tui_palette_attr(THEME_SLOT_BASE);
    tui_fill(box, base);
    char title[64];
    int high = approval_queue_high_count(m->queue);
    snprintf(title, sizeof(title), " \xF0\x9F\xA4\x96 Agentic%s ", high ? "  !! HIGH waiting" : "");
    tui_box(box, title, high ? tui_palette_attr(THEME_SLOT_WARN) | ATTR_BOLD : tui_palette_attr(THEME_SLOT_BORDER));
    tui_fill((UiRect){ box.y + 1, box.x + 1, box.h - 2, box.w - 2 }, base);   /* tui_box fills with the border's colours */
    draw_tabs(p, m, box.y + 1, box, base);
    clamp(p, m);
    int inner_h = box.h - 5;
    int detail_h = p->expanded ? inner_h * 2 / 3 : inner_h / 3;
    if (p->view == AGENTS_VIEW_PERMISSIONS) detail_h = inner_h - PERMISSION_COUNT - 1;
    if (detail_h < 3) detail_h = inner_h > 6 ? 3 : 0;
    UiRect list = { box.y + 3, box.x + 2, inner_h - detail_h - (detail_h ? 1 : 0), box.w - 4 };
    UiRect detail = { list.y + list.h + 1, box.x + 2, detail_h, box.w - 4 };
    p->list_rect = list;
    for (int x = box.x + 1; detail_h && x < box.x + box.w - 1; x++) tui_text(detail.y - 1, x, 1, "\xE2\x94\x80", tui_palette_attr(THEME_SLOT_BORDER));
    switch (p->view) {
        case AGENTS_VIEW_QUEUE:       draw_queue(p, m, list, detail, base); break;
        case AGENTS_VIEW_AGENTS:      draw_agents(p, m, list, detail, base); break;
        case AGENTS_VIEW_LOG:         draw_log(p, m, list, detail, base); break;
        case AGENTS_VIEW_PERMISSIONS: draw_permissions(p, m, list, detail, base); break;
        default: break;
    }
    const char *footer = p->hint[0] ? p->hint : keys_for(p);
    tui_text_center(box.y + box.h - 1, box.x, box.w, footer, p->hint[0] ? tui_palette_attr(THEME_SLOT_WARN) : tui_palette_attr(THEME_SLOT_BORDER));
    if (p->confirming) draw_confirm(p, m, box);
    if (!p->editing && !p->editing_setting && !p->searching) p->caret.visible = 0;
}
