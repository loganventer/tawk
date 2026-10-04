#include "clients/tui/chat_list_view.h"
#include "clients/tui/portrait_view.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "engines/chat_match.h"
#include "utilities/clock_util.h"
#include "utilities/str_util.h"

#include <ncurses.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <wchar.h>
#include <strings.h>

/* Lines of content per chat, and the blank lines that separate chats. */
static int content_rows(const ChatListView *v)   { return v->compact ? 1 : 2; }
static int rows_per_entry(const ChatListView *v) {
    int gap = v->spacing < 0 ? 0 : v->spacing > 2 ? 2 : v->spacing;
    return content_rows(v) + gap;
}

#define MARGIN "  "   /* left margin before names and previews */
#define PORTRAIT_COLS 4
#define PORTRAIT_MARGIN "        "   /* room for a portrait: 2 + 4 columns + 2 */

static int shows_portraits(const ChatListView *v) { return v->portraits && !v->compact; }

static ChatFolder folder_of(const Chat *c) {
    if (c->is_locked > 0) return CHAT_FOLDER_LOCKED;
    if (c->is_archived > 0) return CHAT_FOLDER_ARCHIVED;
    return CHAT_FOLDER_CHATS;
}

static int matches(const ChatListView *v, const Chat *c) { return chat_match_filter(c, v->filter); }

static void push(ChatListView *v, ChatListEntry e) {
    if (v->entry_count < CHAT_LIST_MAX_ENTRIES) v->entries[v->entry_count++] = e;
}

void chat_list_view_init(ChatListView *v) {
    memset(v, 0, sizeof(*v));
    v->drop_pinned = -1;
}

void chat_list_view_sync(ChatListView *v, const Chat *chats, int count) {
    v->entry_count = 0;
    if (v->folder == CHAT_FOLDER_CHATS && !v->filter[0]) {
        ChatListEntry archived = { CHAT_LIST_ENTRY_ARCHIVED, -1, 0, 0 };
        ChatListEntry locked = { CHAT_LIST_ENTRY_LOCKED, -1, 0, 0 };
        for (int i = 0; i < count; i++) {
            ChatFolder f = folder_of(&chats[i]);
            ChatListEntry *e = f == CHAT_FOLDER_ARCHIVED ? &archived : f == CHAT_FOLDER_LOCKED ? &locked : NULL;
            if (!e) continue;
            e->count++;
            if (chats[i].unread > 0) e->unread++;
        }
        if (archived.count) push(v, archived);
        if (locked.count) push(v, locked);
    } else if (v->folder != CHAT_FOLDER_CHATS) {
        push(v, (ChatListEntry){ CHAT_LIST_ENTRY_BACK, -1, 0, 0 });
    }
    /* In the chats folder, pinned chats get their own group with a header,
     * and the rest a second one; either can be folded away. */
    int grouped = v->folder == CHAT_FOLDER_CHATS && !v->filter[0];
    ChatListEntry pinned = { CHAT_LIST_ENTRY_PINNED, -1, 0, 0 }, others = { CHAT_LIST_ENTRY_OTHERS, -1, 0, 0 };
    for (int i = 0; grouped && i < count; i++) {
        if (folder_of(&chats[i]) != CHAT_FOLDER_CHATS) continue;
        ChatListEntry *g = chats[i].is_pinned ? &pinned : &others;
        g->count++;
        if (chats[i].unread > 0) g->unread++;
    }
    grouped = grouped && (pinned.count > 0 || v->dragging);   /* an empty Pinned group to drop into */
    for (int pass = 0; pass < (grouped ? 2 : 1); pass++) {
        int want_pinned = pass == 0;
        if (grouped) push(v, want_pinned ? pinned : others);
        if (grouped && (want_pinned ? v->pinned_collapsed : v->others_collapsed)) continue;
        for (int i = 0; i < count; i++) {
            /* A filter searches every folder, so archived chats can still be found. */
            if (!v->filter[0] && folder_of(&chats[i]) != v->folder) continue;
            if (v->filter[0] && !chat_match_searchable(&chats[i], v->folder == CHAT_FOLDER_LOCKED)) continue;
            if (!matches(v, &chats[i])) continue;
            if (grouped && (chats[i].is_pinned != 0) != want_pinned) continue;
            push(v, (ChatListEntry){ CHAT_LIST_ENTRY_CHAT, i, 0, 0 });
        }
    }
    if (v->selected >= v->entry_count) v->selected = v->entry_count ? v->entry_count - 1 : 0;
}

void chat_list_view_move(ChatListView *v, int delta) {
    v->selected += delta;
    if (v->selected >= v->entry_count) v->selected = v->entry_count - 1;
    if (v->selected < 0) v->selected = 0;
}

const Chat *chat_list_view_selected_chat(const ChatListView *v, const Chat *chats) {
    if (v->selected < 0 || v->selected >= v->entry_count) return NULL;
    const ChatListEntry *e = &v->entries[v->selected];
    return e->kind == CHAT_LIST_ENTRY_CHAT ? &chats[e->chat] : NULL;
}

const char *chat_list_view_selected_jid(const ChatListView *v, const Chat *chats) {
    const Chat *c = chat_list_view_selected_chat(v, chats);
    return c ? c->jid : NULL;
}

/* Whether two rows are the same chat. Where one account is listed the rows
 * name no account and the JID says it all; where several are, a contact can
 * have a row for each. */
static int same_row(const char *jid, AccountId account, const Chat *c) {
    if (strcmp(c->jid, jid) != 0) return 0;
    return account == ACCOUNT_ID_NONE || c->account == ACCOUNT_ID_NONE || c->account == account;
}

/* The open chat's row. A merged row is open whichever of its accounts the conversation is on. */
static int is_open_row(const ChatListView *v, const Chat *c) {
    if (strcmp(c->jid, v->open_jid) != 0) return 0;
    int merged = c->accounts & (c->accounts - 1);
    return merged || same_row(v->open_jid, v->open_account, c);
}

static void enter_folder(ChatListView *v, ChatFolder folder, const Chat *chats, int count) {
    v->folder = folder;
    v->selected = v->scroll = 0;
    v->filter[0] = '\0';
    v->filtering = 0;
    chat_list_view_sync(v, chats, count);
    if (folder != CHAT_FOLDER_CHATS && v->entry_count > 1) v->selected = 1;   /* first chat, not "back" */
}

const char *chat_list_view_activate(ChatListView *v, const Chat *chats, int count) {
    if (v->selected < 0 || v->selected >= v->entry_count) return NULL;
    const ChatListEntry *e = &v->entries[v->selected];
    switch (e->kind) {
        case CHAT_LIST_ENTRY_ARCHIVED: enter_folder(v, CHAT_FOLDER_ARCHIVED, chats, count); return NULL;
        case CHAT_LIST_ENTRY_LOCKED:   enter_folder(v, CHAT_FOLDER_LOCKED, chats, count); return NULL;
        case CHAT_LIST_ENTRY_BACK:     enter_folder(v, CHAT_FOLDER_CHATS, chats, count); return NULL;
        case CHAT_LIST_ENTRY_PINNED:   v->pinned_collapsed = !v->pinned_collapsed; chat_list_view_sync(v, chats, count); return NULL;
        case CHAT_LIST_ENTRY_OTHERS:   v->others_collapsed = !v->others_collapsed; chat_list_view_sync(v, chats, count); return NULL;
        default:                       return chats[e->chat].jid;
    }
}

int chat_list_view_fold(ChatListView *v, const Chat *chats, int count, int expand) {
    if (v->selected < 0 || v->selected >= v->entry_count) return 0;
    ChatListEntryKind kind = v->entries[v->selected].kind;
    int *flag = kind == CHAT_LIST_ENTRY_PINNED ? &v->pinned_collapsed : kind == CHAT_LIST_ENTRY_OTHERS ? &v->others_collapsed : NULL;
    if (!flag) return 0;
    *flag = !expand;
    chat_list_view_sync(v, chats, count);
    return 1;
}

int chat_list_view_back(ChatListView *v, const Chat *chats, int count) {
    if (v->folder == CHAT_FOLDER_CHATS) return 0;
    enter_folder(v, CHAT_FOLDER_CHATS, chats, count);
    return 1;
}

/* The entry at screen row y, or -1. */
static int entry_at(const ChatListView *v, UiRect r, int y) {
    int top = r.y + (v->filtering || v->filter[0] ? 1 : 0);
    if (y < top) return -1;
    int n = v->scroll + (y - top) / rows_per_entry(v);
    return n >= 0 && n < v->entry_count ? n : -1;
}

int chat_list_view_hit(ChatListView *v, UiRect r, int y) {
    int n = entry_at(v, r, y);
    if (n < 0) return 0;
    v->selected = n;
    return 1;
}

/* ---- dragging chats into and out of the Pinned group -------------------- */

static int drag_allowed(const ChatListView *v) { return v->folder == CHAT_FOLDER_CHATS && !v->filter[0]; }

/* 1 over the Pinned group, 0 over the other chats, -1 elsewhere. */
static int drop_target(const ChatListView *v, const Chat *chats, UiRect r, int y) {
    int n = entry_at(v, r, y);
    if (n < 0) return -1;
    const ChatListEntry *e = &v->entries[n];
    switch (e->kind) {
        case CHAT_LIST_ENTRY_PINNED: return 1;
        case CHAT_LIST_ENTRY_OTHERS: return 0;
        case CHAT_LIST_ENTRY_CHAT:   return chats[e->chat].is_pinned ? 1 : 0;
        default:                     return -1;
    }
}

static const Chat *dragged_chat(const ChatListView *v, const Chat *chats, int count) {
    for (int i = 0; i < count; i++) if (same_row(v->drag_jid, v->drag_account, &chats[i])) return &chats[i];
    return NULL;
}

static void drag_reset(ChatListView *v) {
    v->drag_jid[0] = '\0';
    v->dragging = 0;
    v->drop_pinned = -1;
}

int chat_list_view_drag_begin(ChatListView *v, const Chat *chats) {
    drag_reset(v);
    const Chat *c = drag_allowed(v) ? chat_list_view_selected_chat(v, chats) : NULL;
    if (!c) return 0;
    snprintf(v->drag_jid, sizeof(v->drag_jid), "%s", c->jid);
    v->drag_account = c->account;
    v->drag_from = v->selected;
    return 1;
}

void chat_list_view_drag_move(ChatListView *v, const Chat *chats, int count, UiRect r, int y) {
    if (!v->drag_jid[0]) return;
    if (!v->dragging) {
        if (entry_at(v, r, y) == v->drag_from) return;       /* still a click */
        v->dragging = 1;
        chat_list_view_sync(v, chats, count);                  /* may add the empty Pinned group */
        for (int n = 0; n < v->entry_count; n++) {             /* keep the dragged chat selected */
            const ChatListEntry *e = &v->entries[n];
            if (e->kind == CHAT_LIST_ENTRY_CHAT && same_row(v->drag_jid, v->drag_account, &chats[e->chat])) { v->selected = n; break; }
        }
    }
    v->drop_pinned = drop_target(v, chats, r, y);
}

int chat_list_view_drag_end(ChatListView *v, const Chat *chats, int count, UiRect r, int y, char *jid, unsigned long size) {
    int result = -1;
    if (v->dragging) {
        const Chat *c = dragged_chat(v, chats, count);
        int target = drop_target(v, chats, r, y);
        if (c && target >= 0 && target != (c->is_pinned != 0)) {
            snprintf(jid, size, "%s", c->jid);
            result = target;
        }
    }
    int was_dragging = v->dragging;
    drag_reset(v);
    if (was_dragging) chat_list_view_sync(v, chats, count);   /* drops the empty Pinned group again */
    return result;
}

void chat_list_view_filter_key(ChatListView *v, int wc) {
    size_t len = strlen(v->filter);
    if (wc == KEY_BACKSPACE || wc == 127 || wc == 8) {
        while (len > 0 && ((unsigned char)v->filter[len - 1] & 0xC0) == 0x80) len--;
        if (len > 0) v->filter[len - 1] = '\0';
    } else if (wc >= 32 && wc != 127) {                      /* any character, UTF-8 encoded */
        char bytes[MB_LEN_MAX];
        mbstate_t st;
        memset(&st, 0, sizeof(st));
        size_t n = wcrtomb(bytes, (wchar_t)wc, &st);
        if (n != (size_t)-1 && len + n < sizeof(v->filter)) {
            memcpy(v->filter + len, bytes, n);
            v->filter[len + n] = '\0';
        }
    }
    v->selected = 0;
    v->scroll = 0;
}

const char *chat_list_view_next_unread(ChatListView *v, const Chat *chats) {
    for (int step = 1; step <= v->entry_count; step++) {
        int k = (v->selected + step) % v->entry_count;
        const ChatListEntry *e = &v->entries[k];
        if (e->kind == CHAT_LIST_ENTRY_CHAT && chats[e->chat].unread > 0) {
            v->selected = k;
            return chats[e->chat].jid;
        }
    }
    return NULL;
}

void chat_list_view_reveal(ChatListView *v, const Chat *chats, int count, const char *jid, int unfold) {
    for (int i = 0; i < count; i++) {
        if (strcmp(chats[i].jid, jid) != 0) continue;
        int merged = chats[i].accounts & (chats[i].accounts - 1);
        if (!merged && !same_row(jid, v->open_account, &chats[i])) continue;      /* another account's chat with them */
        if (folder_of(&chats[i]) != v->folder || v->filter[0]) {
            v->filter[0] = '\0';
            v->folder = folder_of(&chats[i]);
        }
        if (unfold && chats[i].is_pinned) v->pinned_collapsed = 0;
        else if (unfold) v->others_collapsed = 0;
        chat_list_view_sync(v, chats, count);
        for (int k = 0; k < v->entry_count; k++) {
            if (v->entries[k].kind == CHAT_LIST_ENTRY_CHAT && v->entries[k].chat == i) { v->selected = k; break; }
        }
        return;
    }
}

/* ---- drawing ------------------------------------------------------------ */

/* A group header: fold arrow, name and count, then a rule across the list,
 * so the pinned chats are clearly set apart from the rest. */
static void render_group(const ChatListView *v, const ChatListEntry *e, int y, UiRect r, int selected, int focused) {
    int pinned = e->kind == CHAT_LIST_ENTRY_PINNED;
    int folded = pinned ? v->pinned_collapsed : v->others_collapsed;
    char title[96];
    snprintf(title, sizeof(title), " %s %s%s", folded ? "\xE2\x96\xB8" : "\xE2\x96\xBE",
             pinned ? "\xF0\x9F\x93\x8C Pinned" : "\xF0\x9F\x92\xAC Chats", "");
    if (v->dragging && v->drop_pinned == pinned) selected = focused = 1;   /* where a dragged chat would land */
    int attr = selected ? tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) | (focused ? ATTR_BOLD : 0)
                        : tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD;
    int rows = rows_per_entry(v);
    int row = rows >= 3 ? y + 1 : y;                  /* a blank line above the header when there is room */
    tui_fill((UiRect){ row, r.x, 1, r.w }, selected ? attr : tui_palette_attr(THEME_SLOT_SIDEBAR));
    int used = tui_text(row, r.x, r.w, title, attr);
    char counts[48];
    if (e->unread) snprintf(counts, sizeof(counts), " %d \xC2\xB7 %d unread ", e->count, e->unread);
    else snprintf(counts, sizeof(counts), " %d ", e->count);
    int right = tui_text_right(row, r.x + r.w - 1, r.w / 2, counts, (selected ? attr : tui_palette_attr(THEME_SLOT_DIM)));
    /* the rule: fills the space between title and counts, and runs full width below */
    int rule = tui_palette_attr(THEME_SLOT_BORDER);
    for (int c = r.x + used + 1; c < r.x + r.w - 1 - right; c++) tui_text(row, c, 1, "\xE2\x94\x80", selected ? attr : rule);
    if (row + 1 < y + rows) for (int c = r.x; c < r.x + r.w; c++) tui_text(row + 1, c, 1, "\xE2\x94\x80", rule);
}

static void render_folder(const ChatListView *v, const ChatListEntry *e, int y, UiRect r, int attr) {
    char title[96], sub[96] = "";
    switch (e->kind) {
        case CHAT_LIST_ENTRY_ARCHIVED:
            snprintf(title, sizeof(title), MARGIN "\xF0\x9F\x97\x84  Archived");
            snprintf(sub, sizeof(sub), MARGIN "%d chat%s", e->count, e->count == 1 ? "" : "s");
            break;
        case CHAT_LIST_ENTRY_LOCKED:
            snprintf(title, sizeof(title), MARGIN "\xF0\x9F\x94\x92  Locked chats");
            snprintf(sub, sizeof(sub), MARGIN "%d chat%s", e->count, e->count == 1 ? "" : "s");
            break;
        default:
            snprintf(title, sizeof(title), MARGIN "\xE2\x86\x90  %s", v->folder == CHAT_FOLDER_ARCHIVED ? "Archived" : "Locked chats");
            snprintf(sub, sizeof(sub), MARGIN "back to chats");
            break;
    }
    int right = 0;
    if (e->unread > 0) {
        char badge[16];
        snprintf(badge, sizeof(badge), " %d ", e->unread);
        right = tui_text_right(y, r.x + r.w - 1, 6, badge, tui_palette_attr(THEME_SLOT_BADGE) | ATTR_BOLD);
    }
    tui_text(y, r.x, r.w - right - 2, title, attr | ATTR_BOLD);
    if (!v->compact) tui_text(y + 1, r.x, r.w - 1, sub, attr | ATTR_DIM);
}

static void render_chat(const ChatListView *v, const Chat *c, int y, UiRect r, int attr, int use_24h) {
    int unread = c->unread > 0 && !is_open_row(v, c);
    int typing = c->typing[0] != '\0';
    char badge[16] = "", when[32];
    if (c->unread > 0) snprintf(badge, sizeof(badge), c->unread_mention ? " @ %d " : " %d ", c->unread > 999 ? 999 : c->unread);
    int badge_attr = c->is_muted && !c->unread_mention ? tui_palette_attr(THEME_SLOT_DIM) : tui_palette_attr(THEME_SLOT_BADGE) | ATTR_BOLD;
    /* Compact rows show only the name and a terse time ("14:05", "y", "mo");
     * unread chats are still marked by the bar and bold name. */
    if (v->compact) clock_format_short(c->last_ts, use_24h, when, sizeof(when));
    else clock_format_relative(c->last_ts, use_24h, when, sizeof(when));
    int right = tui_text_right(y, r.x + r.w - 1, r.w / 2, when, c->unread ? (attr | ATTR_BOLD) : attr);
    /* Which accounts the chat is on, when more than one is listed. */
    for (int i = v->badge_count - 1; v->badge_count > 1 && i >= 0; i--) {
        if (!(c->accounts & (1u << i))) continue;
        right += account_badge_draw_right(&v->badges[i], y, r.x + r.w - 1 - right, r.w / 2);
    }

    char title[192];
    const char *margin = shows_portraits(v) ? PORTRAIT_MARGIN : MARGIN;
    /* 📌 only where there is no Pinned group to show it: search results and other folders. */
    int pin_mark = c->is_pinned && (v->folder != CHAT_FOLDER_CHATS || v->filter[0]);
    snprintf(title, sizeof(title), "%s%s%s%s%s", margin, c->soft_locked ? "\xF0\x9F\x99\x88 " : "", pin_mark ? "\xF0\x9F\x93\x8C " : "",
             c->is_muted ? "\xF0\x9F\x94\x95 " : "", c->name);
    tui_text(y, r.x, r.w - right - 2, title, c->unread ? (attr | ATTR_BOLD) : attr);
    if (!v->compact) {
        int badge_cols = badge[0] ? tui_text_right(y + 1, r.x + r.w - 1, 6, badge, badge_attr) : 0;
        char preview[300];
        /* A soft-locked chat shows nothing of its conversation, not even typing. */
        snprintf(preview, sizeof(preview), "%s%s", margin, c->soft_locked ? "\xF0\x9F\x99\x88 Soft-locked" : typing ? c->typing : c->preview);
        int preview_attr = typing ? tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD
                         : c->has_draft ? tui_palette_attr(THEME_SLOT_WARN) : attr | ATTR_DIM;
        tui_text(y + 1, r.x, r.w - badge_cols - 2, preview, preview_attr);
    }
    /* Drawn last: the accent bar on the left edge marks unread chats. */
    /* The portrait: pixels, blocks or an initials badge (a soft-locked chat shows only the badge). */
    if (shows_portraits(v)) {
        const char *pic = c->soft_locked ? NULL : v->portraits->picture(v->portraits->ctx, c->jid);
        ImagePlacement place;
        UiRect box = { y, r.x + 2, content_rows(v), PORTRAIT_COLS };
        if (portrait_draw(box, c->jid, c->soft_locked ? "?" : c->name, pic, v->thumbs, v->pixel_images, &place) &&
            ((ChatListView *)v)->placement_count < CHAT_LIST_MAX_PORTRAITS) {
            ((ChatListView *)v)->placements[((ChatListView *)v)->placement_count++] = place;
        }
    }
    /* Drawn last: bars on the left edge. The open chat gets the accent colour,
     * unread chats the badge colour. */
    int is_open = is_open_row(v, c);
    if (unread || is_open) {
        int bar = is_open ? tui_palette_attr(THEME_SLOT_ACCENT) | ATTR_BOLD : tui_palette_attr(THEME_SLOT_BADGE) | ATTR_REVERSE;
        for (int row = 0; row < content_rows(v); row++) tui_text(y + row, r.x, 1, is_open ? "\xE2\x96\x88" : "\xE2\x96\x8C", bar);
    }
}

void chat_list_view_render(ChatListView *v, UiRect r, const Chat *chats, int count,
                           int focused, int use_24h, const BlinkState *blink, int64_t now_ms) {
    chat_list_view_sync(v, chats, count);
    v->caret.visible = 0;
    v->placement_count = 0;
    int base = tui_palette_attr(THEME_SLOT_SIDEBAR);
    tui_fill(r, base);
    int top = r.y;
    if (v->filtering || v->filter[0]) {
        char line[96];
        snprintf(line, sizeof(line), " \xF0\x9F\x94\x8D %s", v->filter);
        tui_fill((UiRect){ r.y, r.x, 1, r.w }, tui_palette_attr(THEME_SLOT_COMPOSER));
        int used = tui_text(r.y, r.x, r.w, line, tui_palette_attr(THEME_SLOT_COMPOSER));
        v->caret = (TextCaret){ v->filtering, r.y, r.x + (used < r.w - 1 ? used : r.w - 1) };
        top++;
    }
    if (v->entry_count == 0) {
        tui_text_center(top + 1, r.x, r.w, v->filter[0] ? "No matching chats" : "No chats yet", tui_palette_attr(THEME_SLOT_DIM));
        return;
    }
    int per = rows_per_entry(v);
    int slots = (r.y + r.h - top) / per;
    if (slots < 1) slots = 1;
    if (v->selected < v->scroll) v->scroll = v->selected;
    if (v->selected >= v->scroll + slots) v->scroll = v->selected - slots + 1;

    for (int k = 0; k < slots && v->scroll + k < v->entry_count; k++) {
        int n = v->scroll + k;
        const ChatListEntry *e = &v->entries[n];
        int y = top + k * per;
        int selected = n == v->selected;
        const Chat *c = e->kind == CHAT_LIST_ENTRY_CHAT ? &chats[e->chat] : NULL;
        int is_open = c && is_open_row(v, c);
        int unread = c && c->unread > 0 && !is_open;
        int attr = base;
        /* A row with one bit set belongs to one account; a merged row stands for several. */
        int one_account = c && (c->accounts & (c->accounts - 1)) == 0;
        if (c && blink_state_on_row(blink, c->jid, one_account ? c->account : ACCOUNT_ID_NONE, now_ms)) attr = tui_palette_attr(THEME_SLOT_BLINK);
        else if (selected && focused) attr = tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED) | ATTR_BOLD;
        else if (selected || is_open) attr = tui_palette_attr(THEME_SLOT_SIDEBAR_SELECTED);
        else if (unread) attr = tui_palette_attr(c->is_muted ? THEME_SLOT_SIDEBAR_SELECTED : THEME_SLOT_UNREAD) | ATTR_BOLD;
        if (e->kind == CHAT_LIST_ENTRY_PINNED || e->kind == CHAT_LIST_ENTRY_OTHERS) {
            render_group(v, e, y, r, selected, focused);
            continue;
        }
        tui_fill((UiRect){ y, r.x, content_rows(v), r.w }, attr);   /* the gap lines stay plain */
        if (c) render_chat(v, c, y, r, attr, use_24h);
        else render_folder(v, e, y, r, attr);
    }
}
