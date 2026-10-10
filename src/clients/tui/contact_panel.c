#include "clients/tui/contact_panel.h"
#include "clients/tui/portrait_view.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "utilities/str_util.h"
#include "utilities/utf8_text.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DOT "\xC2\xB7"

typedef enum { LINE_TEXT = 0, LINE_HEADING, LINE_DIM, LINE_WARN } LineKind;
typedef struct Line { char text[300]; LineKind kind; } Line;

static Line s_lines[CONTACT_PANEL_LINES];
static int  s_count;

static void add(LineKind kind, const char *text) {
    if (s_count >= CONTACT_PANEL_LINES) return;
    s_lines[s_count].kind = kind;
    str_copy(s_lines[s_count].text, sizeof(s_lines[0].text), text);
    s_count++;
}

/* Adds `text` wrapped to `cols` columns. */
static void add_wrapped(LineKind kind, const char *text, int cols) {
    TextLine *lines = NULL;
    int n = utf8_wrap(text, cols, &lines);
    for (int i = 0; i < n; i++) {
        char part[300];
        size_t len = lines[i].length < sizeof(part) - 1 ? lines[i].length : sizeof(part) - 1;
        memcpy(part, text + lines[i].offset, len);
        part[len] = '\0';
        add(kind, part);
    }
    free(lines);
}

void contact_panel_open(ContactPanel *p, const Chat *chat, int blocked) {
    memset(p, 0, sizeof(*p));
    str_copy(p->jid, sizeof(p->jid), chat->jid);
    str_copy(p->name, sizeof(p->name), chat->name);
    ContactAction *a = p->actions;
    int n = 0;
    a[n++] = CONTACT_ACTION_VIEW_PHOTO;
    a[n++] = CONTACT_ACTION_SEARCH;
    a[n++] = CONTACT_ACTION_OPTIONS;
    a[n++] = CONTACT_ACTION_SOFT_LOCK;
    a[n++] = CONTACT_ACTION_EXPORT;
    a[n++] = CONTACT_ACTION_EXPORT_MEDIA;
    if (!chat->is_group) a[n++] = blocked ? CONTACT_ACTION_UNBLOCK : CONTACT_ACTION_BLOCK;
    a[n++] = CONTACT_ACTION_CLEAR;
    a[n++] = CONTACT_ACTION_DELETE;
    p->action_count = n;
    p->open = 1;
}

/* The value shown after one of this chat's own settings; NULL for any other action. */
static const char *pref_value(const ContactPanel *p, ContactAction a) {
    switch (a) {
        case CONTACT_ACTION_SEND_FROM:     return p->send_from;
        case CONTACT_ACTION_MERGE:         return p->merge;
        case CONTACT_ACTION_AGENT_ANSWERS: return p->agent_answers;
        case CONTACT_ACTION_SHOW_TRANSCRIPTS: return p->show_transcripts;
        case CONTACT_ACTION_TRANSCRIBE:    return p->transcribe;
        case CONTACT_ACTION_TLDR:          return p->tldr;
        default:                           return NULL;
    }
}

static int has_action(const ContactPanel *p, ContactAction a) {
    for (int i = 0; i < p->action_count; i++) if (p->actions[i] == a) return 1;
    return 0;
}

/* Adds the settings of `prefs` that have something to show and are not listed yet. They go
 * first, above the things that are done once: settings are what a card is opened for. */
static void add_prefs(ContactPanel *p, const ContactAction *prefs, int count) {
    int added = 0;
    for (int k = 0; k < count; k++) {
        const char *value = pref_value(p, prefs[k]);
        if (!value[0] || has_action(p, prefs[k]) || p->action_count >= CONTACT_ACTION_COUNT) continue;
        for (int i = p->action_count; i > added; i--) p->actions[i] = p->actions[i - 1];
        p->actions[added++] = prefs[k];
        p->action_count++;
    }
    if (added) p->selected += added;                       /* what was highlighted stays highlighted */
}

void contact_panel_set_prefs(ContactPanel *p, const char *send_from, const char *merge, const char *agent_answers) {
    str_copy(p->send_from, sizeof(p->send_from), send_from ? send_from : "");
    str_copy(p->merge, sizeof(p->merge), merge ? merge : "");
    str_copy(p->agent_answers, sizeof(p->agent_answers), agent_answers ? agent_answers : "");
    static const ContactAction PREFS[] = { CONTACT_ACTION_SEND_FROM, CONTACT_ACTION_MERGE, CONTACT_ACTION_AGENT_ANSWERS };
    add_prefs(p, PREFS, 3);
}

void contact_panel_set_transcript_prefs(ContactPanel *p, const char *show, const char *transcribe) {
    str_copy(p->show_transcripts, sizeof(p->show_transcripts), show ? show : "");
    str_copy(p->transcribe, sizeof(p->transcribe), transcribe ? transcribe : "");
    static const ContactAction PREFS[] = { CONTACT_ACTION_SHOW_TRANSCRIPTS, CONTACT_ACTION_TRANSCRIBE };
    add_prefs(p, PREFS, 2);
}

void contact_panel_set_tldr_pref(ContactPanel *p, const char *tldr) {
    str_copy(p->tldr, sizeof(p->tldr), tldr ? tldr : "");
    static const ContactAction PREFS[] = { CONTACT_ACTION_TLDR };
    add_prefs(p, PREFS, 1);
}

PopupResult contact_panel_key(ContactPanel *p, int is_key, int ch) {
    if (!is_key && (ch == 27 || ch == 'q')) { p->open = 0; return POPUP_CLOSED; }
    if (is_key && ch == KEY_UP && p->selected > 0) p->selected--;
    else if (is_key && ch == KEY_DOWN && p->selected < p->action_count - 1) p->selected++;
    else if (is_key && ch == KEY_PPAGE) contact_panel_wheel(p, -5);
    else if (is_key && ch == KEY_NPAGE) contact_panel_wheel(p, 5);
    else if ((!is_key && (ch == '\n' || ch == '\r')) || (is_key && ch == KEY_ENTER)) return POPUP_CHOSEN;
    else return POPUP_NONE;
    return POPUP_CHANGED;
}

PopupResult contact_panel_click(ContactPanel *p, int y, int x) {
    if (!ui_rect_contains(p->last_rect, y, x)) { p->open = 0; return POPUP_CLOSED; }
    if (ui_rect_contains(p->action_rect, y, x)) {
        int k = y - p->action_rect.y;
        if (k >= 0 && k < p->action_count) { p->selected = k; return POPUP_CHOSEN; }
    }
    return POPUP_NONE;
}

void contact_panel_wheel(ContactPanel *p, int delta) {
    p->scroll += delta;
    if (p->scroll > p->detail_lines - 1) p->scroll = p->detail_lines - 1;
    if (p->scroll < 0) p->scroll = 0;
}

ContactAction contact_panel_choice(const ContactPanel *p) { return p->actions[p->selected]; }

int contact_panel_hit_portrait(const ContactPanel *p, int y, int x) { return ui_rect_contains(p->portrait_rect, y, x); }

/* "27821234567@s.whatsapp.net" -> "+27821234567" */
static void phone_of(const char *jid, char *out, size_t size) {
    size_t n = strcspn(jid, "@:");
    if (strstr(jid, "@s.whatsapp.net")) snprintf(out, size, "+%.*s", (int)n, jid);
    else out[0] = '\0';
}

static void day_of(int64_t ts, char *out, size_t size) {
    time_t t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, size, "%d %B %Y", &tm);
}

static void build_details(const Chat *c, const ContactProfile *pr, int cols,
                          const char *(*member_name)(void *, const char *), void *names_ctx) {
    s_count = 0;
    char buf[400];
    if (pr && pr->blocked) add(LINE_WARN, "\xF0\x9F\x9A\xAB You blocked this contact");
    if (c->soft_locked) add(LINE_DIM, "\xF0\x9F\x99\x88 Soft-locked in tawk");
    if (c->is_muted) add(LINE_DIM, "\xF0\x9F\x94\x95 Muted");
    if (c->is_pinned) add(LINE_DIM, "\xF0\x9F\x93\x8C Pinned");
    if (c->theme[0]) { snprintf(buf, sizeof(buf), "\xF0\x9F\x8E\xA8 Theme: %s", c->theme); add(LINE_DIM, buf); }
    if (s_count) add(LINE_TEXT, "");
    if (!pr) { add(LINE_DIM, "Asking WhatsApp for the details\xE2\x80\xA6"); return; }
    if (!c->is_group) {
        add(LINE_HEADING, "About");
        add_wrapped(LINE_TEXT, pr->about[0] ? pr->about : "Not shared", cols);
        if (pr->is_business || pr->verified_name[0]) {
            add(LINE_TEXT, "");
            add(LINE_HEADING, "Business");
            if (pr->verified_name[0]) { snprintf(buf, sizeof(buf), "\xE2\x9C\x94 %s", pr->verified_name); add(LINE_TEXT, buf); }
            if (pr->business_category[0]) add_wrapped(LINE_TEXT, pr->business_category, cols);
            if (pr->business_address[0]) { snprintf(buf, sizeof(buf), "\xF0\x9F\x93\x8D %s", pr->business_address); add_wrapped(LINE_TEXT, buf, cols); }
            if (pr->business_email[0]) { snprintf(buf, sizeof(buf), "\xE2\x9C\x89 %s", pr->business_email); add(LINE_TEXT, buf); }
        }
        return;
    }
    add(LINE_HEADING, "Description");
    add_wrapped(LINE_TEXT, pr->group_description[0] ? pr->group_description : "No description", cols);
    if (pr->group_created > 0) {
        char day[48];
        day_of(pr->group_created, day, sizeof(day));
        const char *owner = pr->group_owner[0] && member_name ? member_name(names_ctx, pr->group_owner) : "";
        if (owner[0]) snprintf(buf, sizeof(buf), "Created by %s on %s", owner, day);
        else snprintf(buf, sizeof(buf), "Created on %s", day);
        add(LINE_TEXT, "");
        add_wrapped(LINE_DIM, buf, cols);
    }
    if (pr->participant_count > 0 && pr->participants) {
        add(LINE_TEXT, "");
        snprintf(buf, sizeof(buf), "Members (%d)", pr->participant_count);
        add(LINE_HEADING, buf);
        char *list = strdup(pr->participants);
        char *save = NULL;
        for (char *row = list ? strtok_r(list, "\n", &save) : NULL; row; row = strtok_r(NULL, "\n", &save)) {
            char *tab = strchr(row, '\t');
            int admin = tab && tab[1] == '1';
            if (tab) *tab = '\0';
            const char *who = member_name ? member_name(names_ctx, row) : row;
            snprintf(buf, sizeof(buf), "%s%s", who, admin ? "  \xC2\xB7 admin" : "");
            add(LINE_TEXT, buf);
        }
        free(list);
    }
}

int contact_panel_render(ContactPanel *p, UiRect area, const Chat *c, const ContactProfile *pr, const char *picture,
                         const char *(*member_name)(void *, const char *), void *names_ctx,
                         ThumbnailCache *thumbs, int pixel_images, ImagePlacement *placement) {
    int w = area.w < 58 ? area.w : 58;
    UiRect box = { area.y, area.x + area.w - w, area.h, w };
    p->last_rect = box;
    int base = tui_palette_attr(THEME_SLOT_BASE), dim = tui_palette_attr(THEME_SLOT_DIM);
    int header = tui_palette_attr(THEME_SLOT_HEADER), accent = tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD;
    tui_fill(box, base);
    tui_vline(box.y, box.x, box.h, tui_palette_attr(THEME_SLOT_BORDER));
    UiRect in = { box.y, box.x + 2, box.h, box.w - 3 };

    /* Title */
    tui_fill((UiRect){ in.y, box.x + 1, 1, box.w - 1 }, header);
    tui_text(in.y, in.x, in.w, c->is_group ? "Group info" : "Contact info", header | ATTR_BOLD);
    tui_text_right(in.y, box.x + box.w, 12, "Esc close ", header);

    /* Picture, name, number */
    int prow = box.h >= 30 ? 6 : 4;
    UiRect pic = { in.y + 2, in.x + (in.w - prow * 2) / 2, prow, prow * 2 };
    p->portrait_rect = pic;
    int placed = portrait_draw(pic, c->jid, c->name, c->soft_locked ? NULL : picture, thumbs, pixel_images, placement);
    int y = pic.y + pic.h + 1;
    tui_text_center(y++, in.x, in.w, c->name, base | ATTR_BOLD);
    char sub[160];
    if (c->is_group) snprintf(sub, sizeof(sub), "Group" "%s%d members", pr && pr->participant_count ? " " DOT " " : "",
                              pr ? pr->participant_count : 0);
    else phone_of(c->jid, sub, sizeof(sub));
    if (c->is_group && !(pr && pr->participant_count)) str_copy(sub, sizeof(sub), "Group");
    tui_text_center(y++, in.x, in.w, sub, dim);
    y++;

    /* Actions at the bottom; details scroll in between. */
    int actions_top = box.y + box.h - p->action_count - 1;
    build_details(c, pr, in.w, member_name, names_ctx);
    p->detail_lines = s_count;
    int room = actions_top - 1 - y;
    if (p->scroll > s_count - room) p->scroll = s_count - room;
    if (p->scroll < 0) p->scroll = 0;
    for (int i = 0; i < room && p->scroll + i < s_count; i++) {
        const Line *line = &s_lines[p->scroll + i];
        int attr = line->kind == LINE_HEADING ? accent : line->kind == LINE_DIM ? dim
                 : line->kind == LINE_WARN ? tui_palette_attr(THEME_SLOT_WARN) | ATTR_BOLD : base;
        tui_text(y + i, in.x, in.w, line->text, attr);
    }
    if (s_count > room) tui_text_right(actions_top - 1, box.x + box.w - 1, 20, p->scroll + room < s_count ? "\xE2\x86\x93 more " : "", dim);
    for (int c2 = in.x; c2 < in.x + in.w; c2++) tui_text(actions_top - 1, c2, 1, "\xE2\x94\x80", tui_palette_attr(THEME_SLOT_BORDER));
    p->action_rect = (UiRect){ actions_top, in.x, p->action_count, in.w };
    for (int i = 0; i < p->action_count; i++) {
        ContactAction a = p->actions[i];
        int danger = a == CONTACT_ACTION_BLOCK || a == CONTACT_ACTION_CLEAR || a == CONTACT_ACTION_DELETE;
        int attr = i == p->selected ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) | ATTR_BOLD
                 : danger ? tui_palette_attr(THEME_SLOT_WARN) : base;
        tui_fill((UiRect){ actions_top + i, in.x, 1, in.w }, i == p->selected ? attr : base);
        const char *label = contact_action_label(a);
        if (a == CONTACT_ACTION_SOFT_LOCK && c->soft_locked) label = "\xF0\x9F\x91\x80  Show chat (remove soft lock)";
        char with_value[260];
        const char *value = pref_value(p, a);
        if (value) {
            snprintf(with_value, sizeof(with_value), "%s: %s", label, value);
            label = with_value;
        }
        tui_text(actions_top + i, in.x + 1, in.w - 1, label, attr);
    }
    return placed;
}
