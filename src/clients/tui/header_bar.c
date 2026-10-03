#include "clients/tui/header_bar.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "utilities/app_info.h"
#include "utilities/clock_util.h"

#include <ncurses.h>
#include <stdio.h>
#include <time.h>

/* The tabs: Chats, and Agentic with how many requests wait for you and
 * how many are HIGH risk, in marks as well as colour ("3 !!1"), or "●"
 * while an agent is connected and nothing waits. */
static int draw_tabs(UiRect r, int x, const HeaderModel *m, HeaderHits *hits, int attr) {
    int on = tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) | ATTR_BOLD;
    x += tui_text(r.y, x, r.w - x, "  ", attr);
    int used = tui_text(r.y, x, r.w - x, " \xF0\x9F\x92\xAC Chats ", m->agents_tab_active ? attr : on);
    hits->chats_tab = (UiRect){ r.y, x, 1, used };
    x += used;
    x += tui_text(r.y, x, r.w - x, "\xE2\x94\x82", attr | ATTR_DIM);                  /* │ */
    char label[64];
    if (m->agents_waiting > 0 && m->agents_high > 0) snprintf(label, sizeof(label), " \xF0\x9F\xA4\x96 Agentic %d !!%d ", m->agents_waiting, m->agents_high);
    else if (m->agents_waiting > 0) snprintf(label, sizeof(label), " \xF0\x9F\xA4\x96 Agentic %d ", m->agents_waiting);
    else snprintf(label, sizeof(label), " \xF0\x9F\xA4\x96 Agentic%s ", m->agents_connected ? " \xE2\x97\x8F" : "");
    int tab_attr = m->agents_tab_active ? on
                 : m->agents_high ? tui_palette_attr(THEME_SLOT_WARN) | ATTR_BOLD
                 : m->agents_waiting ? tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD : attr;
    used = tui_text(r.y, x, r.w - x, label, tab_attr);
    hits->agents = (UiRect){ r.y, x, 1, used };
    return x + used;
}

/* Left: menu toggle, app name, the Chats and Agentic tabs, and DND. Right, drawn from the edge inwards:
 * gear, then the connection state emoji and user name, the clock, the +
 * that posts a status, and the unread tally (which blinks). */
void header_bar_render(UiRect r, const HeaderModel *m, HeaderHits *hits) {
    hits->post = hits->profile = hits->statuses = (UiRect){ r.y, 0, 0, 0 };
    int attr = tui_palette_attr(THEME_SLOT_HEADER);
    tui_fill(r, attr);
    int x = r.x;
    x += tui_text(r.y, x, r.w, " \xE2\x98\xB0 ", attr | ATTR_BOLD);          /* ☰ open or collapsed */
    x += tui_text(r.y, x, r.w - x, APP_NAME, attr | ATTR_BOLD);
    hits->chats_tab = hits->agents = (UiRect){ r.y, 0, 0, 0 };
    if (m->show_tabs) x = draw_tabs(r, x, m, hits, attr);
    hits->account = (UiRect){ r.y, 0, 0, 0 };
    if (m->show_tabs && m->account && *m->account) {
        char chip[96];
        snprintf(chip, sizeof(chip), " \xF0\x9F\x91\xA4 %s \xE2\x96\xBE ", m->account);       /* 👤 label ▾ */
        x += tui_text(r.y, x, r.w - x, "  ", attr);
        int used = tui_text(r.y, x, r.w - x, chip, tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) | ATTR_BOLD);
        hits->account = (UiRect){ r.y, x, 1, used };
        x += used;
    }
    if (m->dnd) x += tui_text(r.y, x, r.w - x, "  \xF0\x9F\x94\x95 DND", tui_palette_attr(THEME_SLOT_WARN));

    int right = r.x + r.w;
    right -= tui_text_right(r.y, right, right - x, " \xE2\x9A\x99 ", attr | ATTR_BOLD);

    /* The name and the emoji are drawn apart so the name alone is the click target. */
    if (m->user_name && *m->user_name) {
        char name[160];
        snprintf(name, sizeof(name), "%s  ", m->user_name);
        int used = tui_text_right(r.y, right, right - x, name, attr | ATTR_BOLD);
        hits->profile = (UiRect){ r.y, right - used, 1, used > 2 ? used - 2 : used };
        right -= used;
    }
    char state[48];
    snprintf(state, sizeof(state), "%s ", m->status ? m->status : "");
    right -= tui_text_right(r.y, right, right - x, state, attr | ATTR_BOLD);

    char now[24];
    clock_format_time((int64_t)time(NULL), m->use_24h, now, sizeof(now));
    char clock[32];
    snprintf(clock, sizeof(clock), "%s  ", now);
    right -= tui_text_right(r.y, right, right - x, clock, attr);

    if (m->show_post) {
        int used = tui_text_right(r.y, right, right - x, " + ", attr | ATTR_BOLD);
        hits->post = (UiRect){ r.y, right - used, 1, used };
        right -= used;
        /* Statuses, with how many people have new ones. */
        char ring[32];
        if (m->unseen_statuses > 0) snprintf(ring, sizeof(ring), " \xE2\xAD\x95%d ", m->unseen_statuses);   /* ⭕ */
        else snprintf(ring, sizeof(ring), " \xE2\xAD\x95 ");
        int ring_attr = m->unseen_statuses > 0 ? tui_palette_attr(THEME_SLOT_OK) | ATTR_BOLD : attr;
        used = tui_text_right(r.y, right, right - x, ring, ring_attr);
        hits->statuses = (UiRect){ r.y, right - used, 1, used };
        right -= used;
    }

    if (m->tally && *m->tally) {
        char tally[192];
        snprintf(tally, sizeof(tally), "%s  ", m->tally);
        int tally_attr = m->blink_on ? tui_palette_attr(THEME_SLOT_BLINK) | ATTR_BOLD : attr;
        tui_text_right(r.y, right, right - x, tally, tally_attr);
    }
}

int header_bar_hit_menu(UiRect r, int y, int x) { return y == r.y && x < r.x + 3; }
int header_bar_hit_gear(UiRect r, int y, int x) { return y == r.y && x >= r.x + r.w - 3; }
int header_bar_hit_post(const HeaderHits *h, int y, int x) { return h->post.w > 0 && ui_rect_contains(h->post, y, x); }
int header_bar_hit_statuses(const HeaderHits *h, int y, int x) { return h->statuses.w > 0 && ui_rect_contains(h->statuses, y, x); }
int header_bar_hit_chats_tab(const HeaderHits *h, int y, int x) { return h->chats_tab.w > 0 && ui_rect_contains(h->chats_tab, y, x); }
int header_bar_hit_agents(const HeaderHits *h, int y, int x) { return h->agents.w > 0 && ui_rect_contains(h->agents, y, x); }
int header_bar_hit_account(const HeaderHits *h, int y, int x) { return h->account.w > 0 && ui_rect_contains(h->account, y, x); }
int header_bar_hit_profile(const HeaderHits *h, int y, int x) { return h->profile.w > 0 && ui_rect_contains(h->profile, y, x); }
