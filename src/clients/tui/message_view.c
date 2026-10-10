#include "clients/tui/message_view.h"
#include "clients/tui/portrait_view.h"
#include "clients/tui/status_colour.h"
#include "clients/tui/styled_text_view.h"
#include "clients/tui/typing_indicator.h"
#include "clients/tui/text_veil.h"
#include "clients/tui/tui_draw.h"
#include "clients/tui/tui_palette.h"
#include "utilities/clock_util.h"
#include "utilities/path_util.h"
#include "utilities/str_util.h"
#include "utilities/utf8_text.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Messages from the same person this close together form one group. */
#define GROUP_GAP_SECONDS 300
#define THUMB_MAX_COLS    48
#define THUMB_MAX_ROWS    20

/* The conversation pane draws with its own colour pairs so a chat can have
 * its own theme (see tui_palette_apply_conversation). */
static int conv(ThemeSlot slot) { return tui_palette_conversation_attr(slot); }

void message_view_init(MessageView *v) {
    memset(v, 0, sizeof(*v));
    v->selected = -1;
    v->header_rows = 1;
}

static void forget_styled(MessageView *v) {
    for (int i = 0; i < v->styled_count; i++) styled_text_dispose(&v->styled[i]);
    free(v->styled);
    v->styled = NULL;
    v->styled_count = 0;
}

/* A message's text as shown: formatting marks taken out and mentions named. */
static void format_message(StyledText *out, const Message *m, const MessageViewContext *ctx) {
    styled_text_init(out);
    if (!ctx->formatter || m->deleted || !m->text || !m->text[0]) return;
    if (ctx->formatter->format(ctx->formatter->ctx, m, out) != 0) styled_text_dispose(out);
}

static void forget_quoted(MessageView *v) {
    for (int i = 0; i < v->quoted_count; i++) quoted_status_dispose(&v->quoted[i]);
    free(v->quoted);
    v->quoted = NULL;
    v->quoted_count = 0;
}

/* The status each reply to a status answers, looked up once per layout. */
static void load_quoted(MessageView *v, const Message *msgs, int count, const MessageViewContext *ctx) {
    forget_quoted(v);
    int any = 0;
    for (int i = 0; i < count && !any; i++) any = msgs[i].quoted_status && msgs[i].quoted_id[0];
    if (!any || !ctx->statuses || ctx->veiled) return;
    v->quoted = calloc((size_t)count, sizeof(QuotedStatus));
    if (!v->quoted) return;
    v->quoted_count = count;
    for (int i = 0; i < count; i++) {
        if (msgs[i].quoted_status && !msgs[i].deleted) quoted_status_load(&v->quoted[i], ctx->statuses, msgs[i].quoted_id);
    }
}

static const QuotedStatus *quoted_of(const MessageView *v, int index) {
    return index >= 0 && index < v->quoted_count && v->quoted[index].found ? &v->quoted[index] : NULL;
}

static void forget_transcripts(MessageView *v) {
    for (int i = 0; i < v->transcript_count; i++) transcript_view_dispose(&v->transcripts[i]);
    free(v->transcripts);
    v->transcripts = NULL;
    v->transcript_count = 0;
}

/* The transcript of each voice note, looked up once per layout. */
static void load_transcripts(MessageView *v, const Message *msgs, int count, const MessageViewContext *ctx) {
    forget_transcripts(v);
    int any = 0;
    for (int i = 0; i < count && !any; i++) any = msgs[i].type == MESSAGE_TYPE_AUDIO && !msgs[i].deleted;
    if (!any || !ctx->transcripts) return;
    v->transcripts = calloc((size_t)count, sizeof(TranscriptView));
    if (!v->transcripts) return;
    v->transcript_count = count;
    for (int i = 0; i < count; i++) {
        if (msgs[i].type != MESSAGE_TYPE_AUDIO || msgs[i].deleted) continue;
        transcript_view_load(&v->transcripts[i], ctx->transcripts, &msgs[i], ctx->owners ? ctx->owners[i] : ACCOUNT_ID_NONE);
    }
}

static void forget_summaries(MessageView *v) {
    for (int i = 0; i < v->summary_count; i++) summary_view_dispose(&v->summaries[i]);
    free(v->summaries);
    v->summaries = NULL;
    v->summary_count = 0;
}

/* The TL;DR summary of each text message, looked up once per layout. */
static void load_summaries(MessageView *v, const Message *msgs, int count, const MessageViewContext *ctx) {
    forget_summaries(v);
    if (!ctx->summaries || count <= 0) return;
    v->summaries = calloc((size_t)count, sizeof(SummaryView));
    if (!v->summaries) return;
    v->summary_count = count;
    for (int i = 0; i < count; i++) {
        if (msgs[i].type != MESSAGE_TYPE_TEXT || msgs[i].deleted || !msgs[i].text) continue;
        summary_view_load(&v->summaries[i], ctx->summaries, &msgs[i], ctx->owners ? ctx->owners[i] : ACCOUNT_ID_NONE);
    }
}

static const SummaryView *summary_of(const MessageView *v, int index) {
    return index >= 0 && index < v->summary_count && v->summaries[index].found ? &v->summaries[index] : NULL;
}

int message_view_summarised(const MessageView *v, int index) { return summary_of(v, index) != NULL; }

int message_view_unfolded(const MessageView *v, const char *id) {
    for (int i = 0; i < v->unfolded_count; i++) if (strcmp(v->unfolded[i], id) == 0) return 1;
    return 0;
}

void message_view_toggle_summary(MessageView *v, const char *id) {
    if (!id || !id[0]) return;
    for (int i = 0; i < v->unfolded_count; i++) {
        if (strcmp(v->unfolded[i], id) != 0) continue;
        memmove(v->unfolded[i], v->unfolded[i + 1], (size_t)(v->unfolded_count - i - 1) * sizeof(v->unfolded[0]));
        v->unfolded_count--;
        return;
    }
    if (v->unfolded_count == MESSAGE_VIEW_MAX_UNFOLDED) {         /* full: the one unfolded longest ago folds again */
        memmove(v->unfolded[0], v->unfolded[1], (size_t)(MESSAGE_VIEW_MAX_UNFOLDED - 1) * sizeof(v->unfolded[0]));
        v->unfolded_count--;
    }
    snprintf(v->unfolded[v->unfolded_count++], sizeof(v->unfolded[0]), "%s", id);
}

static const TranscriptView *transcript_of(const MessageView *v, int index) {
    return index >= 0 && index < v->transcript_count && v->transcripts[index].found ? &v->transcripts[index] : NULL;
}

void message_view_dispose(MessageView *v) {
    forget_quoted(v);
    forget_transcripts(v);
    forget_summaries(v);
    forget_styled(v);
    free(v->rows);
    v->rows = NULL;
    v->row_count = v->row_cap = 0;
}

static void push_row(MessageView *v, MessageRow row) {
    if (v->row_count == v->row_cap) {
        int cap = v->row_cap ? v->row_cap * 2 : 256;
        MessageRow *grown = realloc(v->rows, (size_t)cap * sizeof(MessageRow));
        if (!grown) return;
        v->rows = grown;
        v->row_cap = cap;
    }
    v->rows[v->row_count++] = row;
}

static int media_available(const Message *m) {
    return m->media_path[0] && path_is_regular_file(m->media_path);
}

/* Short media line: "📷 Photo ↗", "🔊 0:14 ▶". */
#define VOICE_BAR_CELLS 14

/* "━━━━●─────────": played part, the playhead, the rest. Always the same width. */
static void voice_bar(double fraction, char *out, size_t size) {
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    int head = (int)(fraction * (VOICE_BAR_CELLS - 1) + 0.5);
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < VOICE_BAR_CELLS && used + 4 < size; i++) {
        const char *cell = i < head ? "\xE2\x94\x81" : i == head ? "\xE2\x97\x8F" : "\xE2\x94\x80";   /* ━ ● ─ */
        size_t n = strlen(cell);
        memcpy(out + used, cell, n);
        used += n;
    }
    out[used] = '\0';
}

static void media_label(const Message *m, const char *playing, int64_t playing_ms, char *out, size_t size) {
    int have = media_available(m);
    const char *mark = have ? "\xE2\x86\x97" : "\xE2\xA4\x93";                     /* ↗ or ⤓ */
    const char *emoji = message_type_emoji(m->type);
    switch (m->type) {
        case MESSAGE_TYPE_IMAGE:    snprintf(out, size, "%s Photo %s", emoji, mark); break;
        case MESSAGE_TYPE_VIDEO:    snprintf(out, size, "%s Video %s", emoji, mark); break;
        case MESSAGE_TYPE_DOCUMENT: snprintf(out, size, "%s Document %s", emoji, mark); break;
        case MESSAGE_TYPE_STICKER:  snprintf(out, size, "%s Sticker %s", emoji, mark); break;
        case MESSAGE_TYPE_AUDIO: {
            int is_playing = have && playing && playing[0] && strcmp(playing, m->media_path) == 0;
            const char *state = !have ? "\xE2\xA4\x93" : is_playing ? "\xE2\x96\xA0" : "\xE2\x96\xB6";   /* ⤓ ■ ▶ */
            /* While playing: how far along, and the time left (the time played
             * when the length is unknown); otherwise the length. */
            int shown = m->duration_s;
            double fraction = 0;
            if (is_playing) {
                shown = (int)(playing_ms / 1000);
                if (m->duration_s > 0) {
                    long long left_ms = m->duration_s * 1000LL - playing_ms;
                    shown = left_ms > 0 ? (int)((left_ms + 999) / 1000) : 0;
                    fraction = (double)playing_ms / (m->duration_s * 1000.0);
                }
            }
            char bar[VOICE_BAR_CELLS * 3 + 1];
            voice_bar(fraction, bar, sizeof(bar));
            snprintf(out, size, "%s %s %s %d:%02d", emoji, state, bar, shown / 60, shown % 60);
            break;
        }
        default: out[0] = '\0'; break;
    }
}

/* "14:05", "edited 14:05 ✓✓", "14:05 ✗"; videos shown as a picture add
 * "▶ 0:12" in front, since they have no label row. */
/* In a chat merged across accounts: " · W", the mark of the account message
 * `index` belongs to. Empty anywhere else. */
static void owner_mark(const MessageViewContext *ctx, int index, char *out, size_t size) {
    out[0] = '\0';
    if (!ctx->owners || !ctx->badges) return;
    for (int i = 0; i < ctx->badge_count; i++) {
        if (ctx->badges[i].account != ctx->owners[index]) continue;
        snprintf(out, size, " \xC2\xB7 %s", ctx->badges[i].mark);
        return;
    }
}

static void meta_label(const Message *m, const MessageViewContext *ctx, int index, char *out, size_t size) {
    int use_24h = ctx->use_24h;
    char when[64], clock[16], mark[16];
    clock_format_time(m->timestamp, use_24h, clock, sizeof(clock));
    owner_mark(ctx, index, mark, sizeof(mark));
    if (m->type == MESSAGE_TYPE_VIDEO && !m->deleted) {
        if (m->duration_s > 0) snprintf(when, sizeof(when), "\xE2\x96\xB6 %d:%02d  %s%s", m->duration_s / 60, m->duration_s % 60, clock, mark);
        else snprintf(when, sizeof(when), "\xE2\x96\xB6  %s%s", clock, mark);
    } else {
        snprintf(when, sizeof(when), "%s%s", clock, mark);
    }
    const char *edited = m->edited && !m->deleted ? "edited " : "";
    if (!m->from_me) snprintf(out, size, "%s%s", edited, when);
    else if (m->status == MESSAGE_STATUS_FAILED) snprintf(out, size, "%s \xE2\x9C\x97", when);
    else snprintf(out, size, "%s%s %s", edited, when, message_status_ticks(m->status));
}

#define LINK_THUMB_COLS 16
#define LINK_THUMB_ROWS 8
#define LINK_DESC_LINES 2

#define DELETED_TEXT "\xF0\x9F\x9A\xAB This message was deleted"

/* The text a message's rows point into. */
static const char *shown_text(const MessageView *v, int index, const Message *m) {
    if (m->deleted) return DELETED_TEXT;
    if (index >= 0 && index < v->styled_count && v->styled[index].text) return v->styled[index].text;
    return m->text;
}

/* Whose status a reply answers: "▎Your status", "▎Mom's status". */
static void status_head(const Message *m, const NameResolver *names, const QuotedStatus *q, char *out, size_t size) {
    char who[64] = "";
    if (q && q->update.from_me) { snprintf(out, size, "\xE2\x96\x8EYour status"); return; }
    if (m->quoted_sender[0] && names) names->resolve(names->ctx, m->quoted_sender, who, sizeof(who));
    if (who[0]) snprintf(out, size, "\xE2\x96\x8E%s's status", who);
    else snprintf(out, size, "\xE2\x96\x8EStatus");
}

/* "▎Mom: "There is leftover lasagne…"" fitted to `cols`, so the closing
 * quotation mark always shows. A reply to a status that is still kept shows
 * only whose status it is (the status itself follows); one that is gone
 * shows the words WhatsApp quoted. */
static void quote_label(const Message *m, const NameResolver *names, const QuotedStatus *q, int cols, char *out, size_t size) {
    char who[64] = "", head[96];
    if (m->quoted_status) {
        status_head(m, names, q, head, sizeof(head));
        if (q) { str_copy(out, size, head); return; }
        str_copy(head + strlen(head), sizeof(head) - strlen(head), ": \"");
    } else {
        if (m->quoted_sender[0] && names) names->resolve(names->ctx, m->quoted_sender, who, sizeof(who));
        snprintf(head, sizeof(head), "\xE2\x96\x8E%s%s\"", who, who[0] ? ": " : "");
    }
    const char *text = m->quoted_text && m->quoted_text[0] ? m->quoted_text : "\xE2\x80\xA6";
    char flat[200];
    str_copy(flat, sizeof(flat), text);
    for (char *c = flat; *c; c++) if (*c == '\n' || *c == '\r' || *c == '\t') *c = ' ';
    int room = cols - utf8_columns(head) - 1;                  /* the closing mark */
    int used = 0;
    size_t fit = utf8_fit(flat, strlen(flat), room > 0 ? room : 0, &used);
    if (fit < strlen(flat) && fit > 0) {                        /* cut: end on an ellipsis */
        fit = utf8_fit(flat, strlen(flat), room - 1 > 0 ? room - 1 : 0, &used);
        snprintf(out, size, "%s%.*s\xE2\x80\xA6\"", head, (int)fit, flat);
    } else {
        snprintf(out, size, "%s%s\"", head, flat);
    }
}

#define TIME_RUN_SECONDS 60
#define FORWARDED_LABEL  "\xE2\x86\xAA Forwarded"   /* ↪ */

/* Messages from the same person less than a minute apart share one time:
 * only the last of the run shows it (and its ticks). One with an edit or a
 * failed send keeps its own line; reactions sit on the bubble's edge, so
 * they need no line of their own. */
static int time_shared_with_next(const Message *msgs, int count, int i) {
    if (i + 1 >= count) return 0;
    const Message *a = &msgs[i], *b = &msgs[i + 1];
    if ((a->edited && !a->deleted) || a->status == MESSAGE_STATUS_FAILED) return 0;
    if (!a->text || !a->text[0] || a->deleted) return 0;       /* ticks go on a text line; pictures keep theirs */
    if (a->from_me != b->from_me || strcmp(a->sender_jid, b->sender_jid) != 0) return 0;
    if (clock_local_day(a->timestamp) != clock_local_day(b->timestamp)) return 0;
    return b->timestamp - a->timestamp < TIME_RUN_SECONDS && b->timestamp >= a->timestamp;
}

static int starts_group(const Message *msgs, int i, int new_day) {
    if (i == 0 || new_day) return 1;
    const Message *a = &msgs[i - 1], *b = &msgs[i];
    return a->from_me != b->from_me || strcmp(a->sender_jid, b->sender_jid) != 0 ||
           b->timestamp - a->timestamp > GROUP_GAP_SECONDS;
}

/* The sharpest picture available: the downloaded photo or a frame of the
 * downloaded video, else WhatsApp's small preview, else (videos) a
 * placeholder. *picture receives the source actually used. */
static const Thumbnail *picture_for(const Message *m, const MessageViewContext *ctx, int max_cols, MediaPicture *picture) {
    if (!ctx->thumbs || !media_picture_for(m, ctx->media, 1, picture)) return NULL;
    int link = m->type == MESSAGE_TYPE_TEXT;                  /* a link card's picture stays small */
    int limit = link ? LINK_THUMB_COLS : THUMB_MAX_COLS;
    int cols = max_cols < limit ? max_cols : limit;
    return thumbnail_cache_get(ctx->thumbs, picture, cols, link ? LINK_THUMB_ROWS : THUMB_MAX_ROWS);
}

/* "example.com" from "https://www.example.com/path". */
static void site_of(const char *url, char *out, size_t size) {
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    if (strncmp(p, "www.", 4) == 0) p += 4;
    size_t n = strcspn(p, "/?#:");
    if (n >= size) n = size - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static const Thumbnail *thumbnail_for(const Message *m, const MessageViewContext *ctx, int max_cols) {
    MediaPicture picture;
    return picture_for(m, ctx, max_cols, &picture);
}

static void layout_scheduled(MessageView *v, UiRect r, const MessageViewContext *ctx, int max_inner);

#define STATUS_THUMB_COLS 24
#define STATUS_THUMB_ROWS 8
#define STATUS_TEXT_LINES 3
#define STATUS_CAPTION_LINES 2

/* The picture of a photo or video status a reply answers. */
static const Thumbnail *status_thumb(const QuotedStatus *q, const MessageViewContext *ctx, int max_cols) {
    MediaPicture picture;
    if (!q || !ctx->thumbs || !quoted_status_has_picture(q)) return NULL;
    if (!media_picture_for(&q->picture, ctx->media, 1, &picture)) return NULL;
    return thumbnail_cache_get(ctx->thumbs, &picture, max_cols < STATUS_THUMB_COLS ? max_cols : STATUS_THUMB_COLS, STATUS_THUMB_ROWS);
}

/* The status's words: a text status on its own colour (with a column of
 * padding each side), or a photo's caption. Returns the lines kept. */
static int status_lines(const QuotedStatus *q, int max_inner, TextLine **lines, int *widest) {
    *lines = NULL;
    *widest = 0;
    if (!q || !q->update.text || !q->update.text[0]) return 0;
    int on_colour = !quoted_status_has_picture(q);
    int n = utf8_wrap(q->update.text, on_colour ? max_inner - 2 : max_inner, lines);
    int keep = on_colour ? STATUS_TEXT_LINES : STATUS_CAPTION_LINES;
    if (n > keep) n = keep;
    for (int k = 0; k < n; k++) if ((*lines)[k].columns > *widest) *widest = (*lines)[k].columns;
    if (on_colour) *widest += 2;
    return n;
}

static void layout(MessageView *v, UiRect r, const Message *msgs, int count, const MessageViewContext *ctx) {
    v->row_count = 0;
    forget_styled(v);
    load_quoted(v, msgs, count, ctx);
    load_transcripts(v, msgs, count, ctx);
    load_summaries(v, msgs, count, ctx);
    v->styled = count > 0 ? calloc((size_t)count, sizeof(StyledText)) : NULL;
    if (v->styled) {
        v->styled_count = count;
        for (int i = 0; i < count; i++) format_message(&v->styled[i], &msgs[i], ctx);
    }
    int max_inner = r.w * 3 / 4 - 2;
    if (max_inner > 72) max_inner = 72;
    if (max_inner < 10) max_inner = r.w - 4 > 4 ? r.w - 4 : 4;
    v->thumb_cols = max_inner;
    int64_t last_day = -1;

    for (int i = 0; i < count; i++) {
        const Message *m = &msgs[i];
        int64_t day = clock_local_day(m->timestamp);
        int new_day = day != last_day;
        if (new_day) {
            push_row(v, (MessageRow){ i, MESSAGE_ROW_DAY, 0, r.w, 0, 0, 0, 0 });
            last_day = day;
        }
        int group_start = starts_group(msgs, i, new_day);

        char media[96], meta[80], quote[320];
        media_label(m, ctx->playing_path, ctx->playing_ms, media, sizeof(media));
        meta_label(m, ctx, i, meta, sizeof(meta));
        int has_quote = m->quoted_id[0] || (m->quoted_text && m->quoted_text[0]);
        const QuotedStatus *qs = quoted_of(v, i);
        if (has_quote) quote_label(m, ctx->names, qs, max_inner, quote, sizeof(quote));
        const Thumbnail *status_pic = status_thumb(qs, ctx, max_inner);
        TextLine *status_text = NULL;
        int status_widest = 0, n_status = status_lines(qs, max_inner, &status_text, &status_widest);
        const Thumbnail *thumb = thumbnail_for(m, ctx, max_inner);
        if (thumb) media[0] = '\0';          /* photos and videos show the picture; click opens them */
        int show_sender = ctx->is_group && !m->from_me && m->sender_name[0] && group_start;

        TextLine *lines = NULL;
        const char *body = shown_text(v, i, m);
        int n_lines = (body && body[0]) ? utf8_wrap(body, max_inner, &lines) : 0;
        /* TL;DR: a long message shows its summary in place of its text, under a line that unfolds it again. */
        const SummaryView *brief = summary_of(v, i);
        int folded = brief && !message_view_unfolded(v, m->id);
        TextLine *brief_lines = NULL;
        int n_brief = 0;
        if (folded) {
            free(lines);
            lines = NULL;
            n_lines = 0;
            n_brief = utf8_wrap(brief->summary.text, max_inner, &brief_lines);
        }
        if (m->deleted) media[0] = '\0';
        int media_cols = media[0] ? utf8_columns(media) : 0;

        /* The bubble is a rectangle as wide as its widest line; time and
         * ticks sit on the line below it, so they set a minimum width. In a
         * run that shares one time, your message keeps its own ticks at the
         * end of its last line instead. */
        int shared = time_shared_with_next(msgs, count, i);
        int inline_ticks = shared && m->from_me;
        int inner = shared ? 2 : utf8_columns(meta) - 2;
        if (m->reactions[0] && utf8_columns(m->reactions) + 3 > inner) inner = utf8_columns(m->reactions) + 3;
        if (media_cols > inner) inner = media_cols;
        if (thumb && thumb->cols > inner) inner = thumb->cols;
        /* A link card: the page's title, a little of its description, the site. */
        TextLine *desc_lines = NULL;
        int n_desc = 0, has_link = m->link && !m->deleted && (m->link->title[0] || m->link->description[0]);
        if (has_link) {
            char site[128];
            site_of(m->link->url, site, sizeof(site));
            int tc = utf8_columns(m->link->title), sc = utf8_columns(site);
            if (tc > inner) inner = tc;
            if (sc > inner) inner = sc;
            if (m->link->description[0]) n_desc = utf8_wrap(m->link->description, max_inner, &desc_lines);
            if (n_desc > LINK_DESC_LINES) n_desc = LINK_DESC_LINES;
            for (int k = 0; k < n_desc; k++) if (desc_lines[k].columns > inner) inner = desc_lines[k].columns;
        }
        if (has_quote) {
            int qc = utf8_columns(quote);
            if (qc > inner) inner = qc < max_inner ? qc : max_inner;
        }
        if (status_pic && status_pic->cols > inner) inner = status_pic->cols;
        if (status_widest > inner) inner = status_widest;
        if (brief && utf8_columns(summary_view_head(folded)) > inner) inner = utf8_columns(summary_view_head(folded));
        for (int k = 0; k < n_brief; k++) if (brief_lines[k].columns > inner) inner = brief_lines[k].columns;
        /* A voice note's transcript: a few lines of its words, in the same bubble under the play line. */
        const TranscriptView *spoken = transcript_of(v, i);
        TextLine *spoken_lines = NULL;
        int spoken_cut = 0;
        int n_spoken = spoken ? transcript_view_wrap(spoken, max_inner, ctx->transcript_lines, &spoken_lines, &spoken_cut) : 0;
        for (int k = 0; k < n_spoken; k++) {
            int cols = spoken_lines[k].columns + (spoken_cut && k == n_spoken - 1 ? 2 : 0);
            if (cols > inner) inner = cols;
        }
        if (show_sender && utf8_columns(m->sender_name) > inner) inner = utf8_columns(m->sender_name);
        int forwarded = m->forwarded && !m->deleted;
        if (forwarded && utf8_columns(FORWARDED_LABEL) > inner) inner = utf8_columns(FORWARDED_LABEL);
        for (int k = 0; k < n_lines; k++) if (lines[k].columns > inner) inner = lines[k].columns;
        if (inline_ticks && n_lines > 0 && lines[n_lines - 1].columns + 3 > inner) inner = lines[n_lines - 1].columns + 3;
        if (inner > max_inner) inner = max_inner;
        if (inner < 2) inner = 2;
        int width = inner + 2;
        int x = m->from_me ? r.w - width - 1 : 1;
        if (x < 0) x = 0;

        push_row(v, (MessageRow){ i, MESSAGE_ROW_EDGE_TOP, x, width, 0, 0, 0, 0 });
        if (show_sender) push_row(v, (MessageRow){ i, MESSAGE_ROW_SENDER, x, width, 0, 0, 0, 0 });
        if (forwarded) push_row(v, (MessageRow){ i, MESSAGE_ROW_FORWARDED, x, width, 0, 0, 0, 0 });
        if (has_quote) push_row(v, (MessageRow){ i, MESSAGE_ROW_QUOTE, x, width, 0, 0, 0, 0 });
        for (int t = 0; status_pic && t < status_pic->rows; t++) push_row(v, (MessageRow){ i, MESSAGE_ROW_STATUS_THUMB, x, width, 0, 0, 0, t });
        for (int k = 0; k < n_status; k++) {
            push_row(v, (MessageRow){ i, MESSAGE_ROW_STATUS_TEXT, x, width, status_text[k].offset, status_text[k].length, 0, 0 });
        }
        free(status_text);
        for (int t = 0; thumb && t < thumb->rows; t++) push_row(v, (MessageRow){ i, MESSAGE_ROW_THUMB, x, width, 0, 0, 0, t });
        if (media[0]) push_row(v, (MessageRow){ i, MESSAGE_ROW_MEDIA, x, width, 0, 0, 0, 0 });
        for (int k = 0; k < n_spoken; k++) {
            int cut = spoken_cut && k == n_spoken - 1;
            push_row(v, (MessageRow){ i, MESSAGE_ROW_TRANSCRIPT, x, width, spoken_lines[k].offset, spoken_lines[k].length, cut, 0 });
        }
        free(spoken_lines);
        if (has_link) {
            if (m->link->title[0]) push_row(v, (MessageRow){ i, MESSAGE_ROW_LINK_TITLE, x, width, 0, 0, 0, 0 });
            for (int k = 0; k < n_desc; k++) {
                push_row(v, (MessageRow){ i, MESSAGE_ROW_LINK_DESC, x, width, desc_lines[k].offset, desc_lines[k].length, 0, 0 });
            }
            push_row(v, (MessageRow){ i, MESSAGE_ROW_LINK_SITE, x, width, 0, 0, 0, 0 });
        }
        free(desc_lines);
        if (brief) push_row(v, (MessageRow){ i, MESSAGE_ROW_TLDR_HEAD, x, width, 0, 0, 0, folded });
        for (int k = 0; k < n_brief; k++) {
            push_row(v, (MessageRow){ i, MESSAGE_ROW_TLDR, x, width, brief_lines[k].offset, brief_lines[k].length, 0, 0 });
        }
        free(brief_lines);
        for (int k = 0; k < n_lines; k++) {
            int last = k == n_lines - 1;
            push_row(v, (MessageRow){ i, MESSAGE_ROW_TEXT, x, width, lines[k].offset, lines[k].length, last && inline_ticks, 0 });
        }
        push_row(v, (MessageRow){ i, MESSAGE_ROW_EDGE_BOTTOM, x, width, 0, 0, 0, 0 });
        /* the line under the bubble: time and ticks, or just a gap inside a run */
        push_row(v, (MessageRow){ i, shared ? MESSAGE_ROW_GAP : MESSAGE_ROW_META, x, width, 0, 0, 0, 0 });
        free(lines);
    }
    layout_scheduled(v, r, ctx, max_inner);
}

/* Messages you scheduled come last, as dim bubbles on your side with the
 * time they will go. They are not messages yet, so they cannot be selected. */
static void layout_scheduled(MessageView *v, UiRect r, const MessageViewContext *ctx, int max_inner) {
    for (int k = 0; k < ctx->scheduled_count; k++) {
        const ScheduledMessage *s = &ctx->scheduled[k];
        const char *text = s->text ? s->text : "";
        TextLine *lines = NULL;
        int n = utf8_wrap(text, max_inner, &lines);
        int inner = 12;
        for (int i = 0; i < n; i++) if (lines[i].columns > inner) inner = lines[i].columns;
        if (inner > max_inner) inner = max_inner;
        int width = inner + 2, x = r.w - width - 1;
        if (x < 0) x = 0;
        push_row(v, (MessageRow){ k, MESSAGE_ROW_SCHEDULED, x, 0, 0, 0, 0, 0 });              /* a row of space above */
        for (int i = 0; i < n; i++) push_row(v, (MessageRow){ k, MESSAGE_ROW_SCHEDULED, x, width, lines[i].offset, lines[i].length, 0, 0 });
        push_row(v, (MessageRow){ k, MESSAGE_ROW_SCHEDULED_META, x, width, 0, 0, 0, 0 });
        free(lines);
    }
}

static int is_scheduled_row(MessageRowKind kind) {
    return kind == MESSAGE_ROW_SCHEDULED || kind == MESSAGE_ROW_SCHEDULED_META;
}

static void draw_scheduled_row(const MessageRow *row, int y, UiRect r, const MessageViewContext *ctx) {
    const ScheduledMessage *s = &ctx->scheduled[row->message];
    int x = r.x + row->x;
    if (row->kind == MESSAGE_ROW_SCHEDULED_META) {
        char when[48], label[80];
        clock_format_upcoming(s->due_at, ctx->use_24h, when, sizeof(when));
        snprintf(label, sizeof(label), "\xF0\x9F\x95\x93 %s", when);   /* 🕓 */
        tui_text_right(y, x + row->width, row->width + 8, label, conv(THEME_SLOT_DIM));
        return;
    }
    if (row->width <= 0) return;
    int bubble = conv(THEME_SLOT_BUBBLE_ME) | ATTR_DIM;
    tui_fill((UiRect){ y, x, 1, row->width }, bubble);
    if (ctx->veiled) text_veil_text(y, x + 1, row->width - 2, s->text + row->offset, row->length, bubble);
    else tui_text_n(y, x + 1, row->width - 2, s->text + row->offset, row->length, bubble);
}

/* Photos whose preview rows are all on screen get a pixel image instead of
 * half blocks; partly visible ones keep the blocks. */
static void place_images(MessageView *v, UiRect body, int start, const Message *msgs, const MessageViewContext *ctx) {
    if (!ctx->pixel_images || ctx->veiled) return;
    for (int k = 0; k < body.h && v->placement_count < MESSAGE_VIEW_MAX_PLACEMENTS; k++) {
        int i = start + k;
        if (i < 0 || i >= v->row_count) continue;
        const MessageRow *row = &v->rows[i];
        if (row->kind != MESSAGE_ROW_THUMB || row->sub != 0) continue;
        const Message *m = &msgs[row->message];
        MediaPicture picture;
        const Thumbnail *t = picture_for(m, ctx, v->thumb_cols, &picture);
        if (!t || k + t->rows > body.h) continue;
        ImagePlacement *p = &v->placements[v->placement_count++];
        memset(p, 0, sizeof(*p));
        p->message = row->message;
        p->y = body.y + k;
        p->x = body.x + row->x + 1;
        p->cols = t->cols;
        p->rows = t->rows;
        p->attr = conv(m->from_me ? THEME_SLOT_BUBBLE_ME : THEME_SLOT_BUBBLE_THEM);
        p->source = (int)picture.source;
        p->page = picture.page;
        snprintf(p->path, sizeof(p->path), "%s", picture.path);
        snprintf(p->id, sizeof(p->id), "%s", m->id);
    }
}

static int is_placed(const MessageView *v, int message) {
    for (int i = 0; i < v->placement_count; i++) if (v->placements[i].message == message) return 1;
    return 0;
}

static void draw_row(const MessageView *v, const MessageRow *row, int y, UiRect r, const Message *m, int selected,
                     const MessageViewContext *ctx) {
    int bubble = conv(m->from_me ? THEME_SLOT_BUBBLE_ME : THEME_SLOT_BUBBLE_THEM);
    int x = r.x + row->x;
    char buf[320];
    switch (row->kind) {
        case MESSAGE_ROW_DAY: {
            char day[48], label[64];
            clock_format_day(m->timestamp, day, sizeof(day));
            snprintf(label, sizeof(label), " %s ", day);
            tui_text_center(y, r.x, r.w, label, conv(THEME_SLOT_DAY_SEPARATOR));
            return;
        }
        case MESSAGE_ROW_GAP:
        case MESSAGE_ROW_REACTIONS:
            return;
        case MESSAGE_ROW_EDGE_TOP:
        case MESSAGE_ROW_EDGE_BOTTOM: {
            /* Half blocks in the bubble colour give the bubble a solid,
             * padded rectangle without a full blank row. */
            const char *glyph = row->kind == MESSAGE_ROW_EDGE_TOP ? "\xE2\x96\x84" : "\xE2\x96\x80";
            attrset(tui_palette_bubble_edge_attr(m->from_me));
            for (int c = 0; c < row->width; c++) mvaddstr(y, x + c, glyph);
            attrset(A_NORMAL);
            if (selected && x > r.x) tui_text(y, x - 1, 1, "\xE2\x96\x8C", conv(THEME_SLOT_ACCENT) | ATTR_BOLD);
            /* Reactions sit on the bubble's bottom edge, as on the phone, on
             * the side facing the middle of the conversation: the left of
             * your bubbles, the right of everyone else's. */
            if (row->kind == MESSAGE_ROW_EDGE_BOTTOM && m->reactions[0] && !ctx->veiled) {
                char chip[128];
                snprintf(chip, sizeof(chip), " %s ", m->reactions);
                /* In the bubble's own colours, so it reads as part of it in every theme. */
                int attr = conv(m->from_me ? THEME_SLOT_BUBBLE_ME : THEME_SLOT_BUBBLE_THEM) | ATTR_BOLD;
                if (m->from_me) tui_text(y, x + 1, row->width - 2, chip, attr);
                else tui_text_right(y, x + row->width - 1, row->width - 2, chip, attr);
            }
            return;
        }
        case MESSAGE_ROW_META: {
            /* The line between messages: reactions on the left, then time and
             * ticks right-aligned to the bubble's right edge. */
            meta_label(m, ctx, row->message, buf, sizeof(buf));
            if (ctx->veiled) {
                int cols = utf8_columns(buf);
                text_veil_draw(y, x + row->width - cols, cols, conv(THEME_SLOT_DIM));
                return;
            }
            int meta_attr = conv(m->status == MESSAGE_STATUS_READ ? THEME_SLOT_ACCENT : THEME_SLOT_DIM) |
                            (m->status == MESSAGE_STATUS_FAILED ? ATTR_BOLD : 0);
            if (m->from_me) tui_text_right(y, x + row->width, row->width, buf, meta_attr);
            else tui_text(y, x, row->width, buf, meta_attr);
            return;
        }
        default:
            break;
    }
    tui_fill((UiRect){ y, x, 1, row->width }, bubble);
    if (selected && x > r.x) tui_text(y, x - 1, 1, "\xE2\x96\x8C", conv(THEME_SLOT_ACCENT) | ATTR_BOLD);
    int room = row->width - 2;
    if (ctx->veiled) {                                  /* soft lock: shapes only */
        int veil = bubble | ATTR_DIM;
        switch (row->kind) {
            case MESSAGE_ROW_SENDER: text_veil_text(y, x + 1, room, m->sender_name, strlen(m->sender_name), veil); break;
            case MESSAGE_ROW_TEXT:
                text_veil_text(y, x + 1, room, shown_text(v, row->message, m) + row->offset, row->length, veil);
                break;
            case MESSAGE_ROW_THUMB: {
                const Thumbnail *t = thumbnail_for(m, ctx, v->thumb_cols);
                text_veil_draw(y, x + 1, t ? t->cols : room, veil);
                break;
            }
            case MESSAGE_ROW_TLDR: {
                const SummaryView *brief = summary_of(v, row->message);
                if (brief) summary_view_draw_veiled(brief, y, x + 1, room, row->offset, row->length, veil);
                break;
            }
            case MESSAGE_ROW_TRANSCRIPT: {
                const TranscriptView *spoken = transcript_of(v, row->message);
                if (spoken) transcript_view_draw_veiled(spoken, y, x + 1, room, row->offset, row->length, veil);
                break;
            }
            default:                 text_veil_draw(y, x + 1, room * 2 / 3, veil); break;
        }
        return;
    }
    switch (row->kind) {
        case MESSAGE_ROW_SENDER:
            tui_text(y, x + 1, room, m->sender_name, conv(THEME_SLOT_SENDER) | ATTR_BOLD);
            break;
        case MESSAGE_ROW_FORWARDED:
            tui_text(y, x + 1, room, FORWARDED_LABEL, bubble | ATTR_DIM);
            break;
        case MESSAGE_ROW_QUOTE:
            quote_label(m, ctx->names, quoted_of(v, row->message), room, buf, sizeof(buf));
            tui_text(y, x + 1, room, buf, bubble | ATTR_DIM | ATTR_BOLD);
            break;
        case MESSAGE_ROW_STATUS_THUMB: {
            const Thumbnail *t = status_thumb(quoted_of(v, row->message), ctx, room);
            if (t && row->sub < t->rows) {
                Thumbnail one = { t->cols, 1, t->top + row->sub * t->cols, t->bottom + row->sub * t->cols };
                thumbnail_draw(&one, y, x + 1);
            }
            break;
        }
        case MESSAGE_ROW_STATUS_TEXT: {
            const QuotedStatus *q = quoted_of(v, row->message);
            if (!q || !q->update.text) break;
            if (quoted_status_has_picture(q)) {                 /* the caption */
                tui_text_n(y, x + 1, room, q->update.text + row->offset, row->length, bubble | ATTR_DIM);
                break;
            }
            int colour = status_colour_attr(q->update.background_argb);
            if (!colour) colour = bubble | A_REVERSE;          /* the terminal cannot show its colour */
            tui_fill((UiRect){ y, x + 1, 1, room }, colour);
            tui_text_n(y, x + 2, room - 2, q->update.text + row->offset, row->length, colour);
            break;
        }
        case MESSAGE_ROW_THUMB: {
            const Thumbnail *t = thumbnail_for(m, ctx, v->thumb_cols);
            if (is_placed(v, row->message)) break;          /* a pixel image goes here */
            if (t && row->sub < t->rows) {
                Thumbnail one = { t->cols, 1, t->top + row->sub * t->cols, t->bottom + row->sub * t->cols };
                thumbnail_draw(&one, y, x + 1);
            }
            break;
        }
        case MESSAGE_ROW_MEDIA:
            media_label(m, ctx->playing_path, ctx->playing_ms, buf, sizeof(buf));
            tui_text(y, x + 1, room, buf, bubble | ATTR_BOLD);
            break;
        case MESSAGE_ROW_TLDR_HEAD:
            summary_view_draw_head(row->sub, y, x + 1, room, bubble);
            break;
        case MESSAGE_ROW_TLDR: {
            const SummaryView *brief = summary_of(v, row->message);
            if (brief) summary_view_draw_line(brief, y, x + 1, room, row->offset, row->length, bubble);
            break;
        }
        case MESSAGE_ROW_TRANSCRIPT: {
            const TranscriptView *spoken = transcript_of(v, row->message);
            if (spoken) transcript_view_draw_line(spoken, y, x + 1, room, row->offset, row->length, row->meta_inline, bubble);
            break;
        }
        case MESSAGE_ROW_LINK_TITLE:
            if (m->link) tui_text(y, x + 1, room, m->link->title, bubble | ATTR_BOLD);
            break;
        case MESSAGE_ROW_LINK_DESC:
            if (m->link) tui_text_n(y, x + 1, room, m->link->description + row->offset, row->length, bubble | ATTR_DIM);
            break;
        case MESSAGE_ROW_LINK_SITE:
            if (m->link) {
                site_of(m->link->url, buf, sizeof(buf));
                tui_text(y, x + 1, room, buf, tui_palette_bubble_accent_attr(m->from_me));
            }
            break;
        case MESSAGE_ROW_TEXT:
            if (m->deleted) {
                tui_text_n(y, x + 1, room, DELETED_TEXT + row->offset, row->length, bubble | ATTR_DIM);
            } else if (row->message < v->styled_count && v->styled[row->message].text) {
                styled_text_view_draw(y, x + 1, room, &v->styled[row->message], row->offset, row->length, bubble,
                                      tui_palette_bubble_accent_attr(m->from_me));
            } else {
                tui_text_n(y, x + 1, room, m->text + row->offset, row->length, bubble);
            }
            if (row->meta_inline) {                         /* this message's own ticks, in a shared-time run */
                const char *ticks = message_status_ticks(m->status);
                int read = m->status == MESSAGE_STATUS_READ;
                tui_text_right(y, x + row->width - 1, 3, ticks, read ? bubble | ATTR_BOLD : bubble | ATTR_DIM);
            }
            break;
        default:
            break;
    }
}

static int is_body_row(MessageRowKind kind) {
    return kind != MESSAGE_ROW_DAY && kind != MESSAGE_ROW_GAP && !is_scheduled_row(kind);
}

/* Puts the view back where message_view_hold found it, now that the array has changed. */
static void restore_held(MessageView *v, int body_h, const Message *msgs, int count) {
    if (!v->held) return;
    v->held = 0;
    int had_selection = v->selected >= 0;
    if (had_selection) v->selected = -1;
    for (int i = 0; i < count; i++) {
        if (had_selection && v->held_selected[0] && strcmp(msgs[i].id, v->held_selected) == 0) v->selected = i;
    }
    for (int i = 0; i < v->row_count; i++) {
        const MessageRow *row = &v->rows[i];
        if (is_scheduled_row(row->kind) || row->message < 0 || row->message >= count) continue;
        if (strcmp(msgs[row->message].id, v->held_top) != 0) continue;
        v->scroll = v->row_count - body_h - (i + v->held_offset);
        if (v->scroll < 0) v->scroll = 0;
        return;
    }
}

void message_view_render(MessageView *v, UiRect r, const Message *msgs, int count, const MessageViewContext *ctx) {
    v->last_rect = r;
    tui_fill(r, conv(THEME_SLOT_CHAT));
    for (int i = 0; i < MESSAGE_VIEW_MAX_SCREEN_ROWS; i++) { v->screen_rows[i] = -1; v->screen_quote[i] = 0; }
    v->placement_count = 0;
    if (r.h < 2 || r.w < 8) return;

    /* Title bar: portrait, chat name, and under it the about text or member count. */
    int header = conv(THEME_SLOT_HEADER);
    int with_portrait = ctx->jid && ctx->jid[0] && r.h >= 6;
    v->header_rows = with_portrait ? 2 : 1;
    v->portrait_rect = (UiRect){ 0, 0, 0, 0 };
    tui_fill((UiRect){ r.y, r.x, v->header_rows, r.w }, header);
    int name_x = r.x + 1;
    if (with_portrait) {
        v->portrait_rect = (UiRect){ r.y, r.x + 1, 2, 4 };
        ImagePlacement place;
        if (portrait_draw(v->portrait_rect, ctx->jid, ctx->title, ctx->portrait, ctx->thumbs, ctx->portrait_pixels && !ctx->veiled, &place) &&
            v->placement_count < MESSAGE_VIEW_MAX_PLACEMENTS) {
            v->placements[v->placement_count++] = place;
        }
        name_x = r.x + 7;
    }
    int used = tui_text(r.y, name_x, r.w - (name_x - r.x) - 1, ctx->title && *ctx->title ? ctx->title : "Select a chat", header | ATTR_BOLD);
    v->title_rect = (UiRect){ r.y, name_x, v->header_rows, used };
    if (with_portrait) {
        const char *second = ctx->status && *ctx->status ? ctx->status : ctx->subtitle ? ctx->subtitle : "";   /* a loading note first */
        int attr = ctx->status && *ctx->status ? conv(THEME_SLOT_OK) | ATTR_BOLD : header | ATTR_DIM;
        tui_text(r.y + 1, name_x, r.w - (name_x - r.x) - 1, second, attr);
    } else if (ctx->status && *ctx->status) {
        char status[96];
        snprintf(status, sizeof(status), "  %s", ctx->status);
        tui_text(r.y, name_x + used, r.w - used - 2, status, conv(THEME_SLOT_OK) | ATTR_BOLD);
    }
    UiRect body = { r.y + v->header_rows, r.x, r.h - v->header_rows, r.w };
    /* Who is typing or recording sits on the last row, just above the input. */
    if (ctx->activity && ctx->activity[0] && body.h > 3) {
        body.h--;
        typing_indicator_draw(body.y + body.h, body.x + 1, body.w - 2, ctx->activity, ctx->activity_phase);
    }
    if ((!msgs || count == 0) && ctx->scheduled_count == 0) {
        tui_text_center(body.y + body.h / 2, body.x, body.w,
                        ctx->title && *ctx->title ? "No messages yet. Say hello!" : "Pick a chat on the left, or press Tab",
                        conv(THEME_SLOT_DIM));
        return;
    }

    layout(v, body, msgs, count, ctx);
    restore_held(v, body.h, msgs, count);
    int max_scroll = v->row_count - body.h;
    if (max_scroll < 0) max_scroll = 0;
    if (v->scroll > max_scroll) v->scroll = max_scroll;
    if (v->scroll < 0) v->scroll = 0;

    /* Keep the selected message on screen (its top when it is taller than the pane). */
    if (v->selected >= count) v->selected = count - 1;
    if (ctx->focused && v->selected >= 0 && v->follow_selection) {
        int first = -1, last = -1;
        for (int i = 0; i < v->row_count; i++) {
            if (v->rows[i].message != v->selected || !is_body_row(v->rows[i].kind)) continue;
            if (first < 0) first = i;
            last = i;
        }
        int top = v->row_count - body.h - v->scroll;
        if (first >= 0 && first < top) v->scroll = v->row_count - body.h - first;
        if (last >= 0 && last >= top + body.h && last - first < body.h) v->scroll = v->row_count - 1 - last;
        if (v->scroll < 0) v->scroll = 0;
    }

    int start = v->row_count - body.h - v->scroll;
    place_images(v, body, start, msgs, ctx);
    for (int k = 0; k < body.h; k++) {
        int i = start + k;
        if (i < 0 || i >= v->row_count) continue;
        const MessageRow *row = &v->rows[i];
        if (is_scheduled_row(row->kind)) { draw_scheduled_row(row, body.y + k, body, ctx); continue; }
        draw_row(v, row, body.y + k, body, &msgs[row->message], ctx->focused && row->message == v->selected, ctx);
        if (k < MESSAGE_VIEW_MAX_SCREEN_ROWS && is_body_row(row->kind)) {
            v->screen_rows[k] = row->message;
            v->screen_quote[k] = row->kind == MESSAGE_ROW_QUOTE;
            v->screen_x0[k] = (short)(body.x + row->x);
            v->screen_x1[k] = (short)(body.x + row->x + row->width);
        }
    }
    v->drawn = 1;
    v->newer_button = (UiRect){ 0, 0, 0, 0 };
    if (v->scroll > 0 || v->has_newer) {
        int w = tui_text_right(body.y + body.h - 1, body.x + body.w - 1, 16, " \xE2\x86\x93 newer ", conv(THEME_SLOT_BADGE) | ATTR_BOLD);
        v->newer_button = (UiRect){ body.y + body.h - 1, body.x + body.w - 1 - w, 1, w };
    }
}

int message_view_visible_range(const MessageView *v, int *first, int *last) {
    *first = *last = -1;
    for (int k = 0; k < MESSAGE_VIEW_MAX_SCREEN_ROWS; k++) {
        int index = v->screen_rows[k];
        if (index < 0) continue;
        if (*first < 0 || index < *first) *first = index;
        if (index > *last) *last = index;
    }
    return *first >= 0;
}

void message_view_hold(MessageView *v, const Message *msgs, int count) {
    v->held = 0;
    v->held_selected[0] = '\0';
    if (!msgs || count <= 0 || v->row_count == 0) return;
    int body_h = v->last_rect.h - v->header_rows;
    int top = v->row_count - body_h - v->scroll;
    if (top < 0) top = 0;
    /* The first row from the top that belongs to a message (not to a scheduled one). */
    while (top < v->row_count && (is_scheduled_row(v->rows[top].kind) || v->rows[top].message < 0 || v->rows[top].message >= count)) top++;
    if (top >= v->row_count) return;
    int index = v->rows[top].message, first = top;
    while (first > 0 && v->rows[first - 1].message == index && !is_scheduled_row(v->rows[first - 1].kind)) first--;
    snprintf(v->held_top, sizeof(v->held_top), "%s", msgs[index].id);
    v->held_offset = top - first;
    if (v->selected >= 0 && v->selected < count) snprintf(v->held_selected, sizeof(v->held_selected), "%s", msgs[v->selected].id);
    v->held = 1;
}

void message_view_release(MessageView *v) { v->held = 0; }

void message_view_scroll(MessageView *v, int delta) {
    v->follow_selection = 0;                 /* scrolling leaves the selection where it is */
    v->scroll += delta;
    if (v->scroll < 0) v->scroll = 0;
}

void message_view_scroll_to_latest(MessageView *v) {
    v->scroll = 0;
    v->selected = -1;
}

void message_view_select(MessageView *v, int count, int delta) {
    if (count <= 0) { v->selected = -1; return; }
    v->follow_selection = 1;
    if (v->selected < 0) v->selected = count;
    v->selected += delta;
    if (v->selected < 0) v->selected = 0;
    if (v->selected >= count) v->selected = count - 1;
}

int message_view_hit_quote(MessageView *v, int y, int x) {
    int index = message_view_hit(v, y, x);
    int k = y - v->last_rect.y - v->header_rows;
    return index >= 0 && k >= 0 && k < MESSAGE_VIEW_MAX_SCREEN_ROWS && v->screen_quote[k] ? index : -1;
}

int message_view_hit_portrait(const MessageView *v, int y, int x) { return ui_rect_contains(v->portrait_rect, y, x); }
int message_view_hit_title(const MessageView *v, int y, int x) { return ui_rect_contains(v->title_rect, y, x); }

int message_view_hit_newer(const MessageView *v, int y, int x) {
    return ui_rect_contains(v->newer_button, y, x);
}

int message_view_hit(MessageView *v, int y, int x) {
    UiRect body = { v->last_rect.y + v->header_rows, v->last_rect.x, v->last_rect.h - v->header_rows, v->last_rect.w };
    if (!ui_rect_contains(body, y, x)) return -1;
    int k = y - body.y;
    if (k >= MESSAGE_VIEW_MAX_SCREEN_ROWS || v->screen_rows[k] < 0) return -1;
    return x >= v->screen_x0[k] && x < v->screen_x1[k] ? v->screen_rows[k] : -1;
}
