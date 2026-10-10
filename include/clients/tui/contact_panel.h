#ifndef APP_CLIENTS_TUI_CONTACT_PANEL_H
#define APP_CLIENTS_TUI_CONTACT_PANEL_H

#include "clients/tui/contact_action.h"
#include "clients/tui/image_placement.h"
#include "clients/tui/popup_result.h"
#include "clients/tui/thumbnail_cache.h"
#include "clients/tui/ui_rect.h"
#include "core/chat.h"
#include "core/contact_profile.h"

#define CONTACT_PANEL_LINES 400

/* Contact or group details, like WhatsApp's info screen: the picture, name,
 * number, about text, business details or group description and members,
 * and a list of actions. Scrolls; the actions are always at the bottom. */
typedef struct ContactPanel {
    int           open;
    char          jid[128];
    char          name[128];
    ContactAction actions[CONTACT_ACTION_COUNT];
    int           action_count;
    int           selected;
    int           scroll;              /* first detail line shown */
    int           detail_lines;        /* lines of details in the last render */
    UiRect        last_rect;
    UiRect        portrait_rect;
    UiRect        action_rect;         /* where the actions were drawn, one per row */
    /* This chat's own settings, as they stand: what the three actions show after their names. Empty hides one. */
    char          send_from[96];
    char          merge[48];
    char          agent_answers[120];
    char          show_transcripts[48];
    char          transcribe[24];
    char          tldr[16];
    char          owner_chat[16];
    char          voice_languages[96];
} ContactPanel;

void          contact_panel_open(ContactPanel *panel, const Chat *chat, int blocked);
/* Says how this chat's own settings stand, adding their actions the first time. An empty text leaves one out. */
void          contact_panel_set_prefs(ContactPanel *panel, const char *send_from, const char *merge, const char *agent_answers);
/* The same for its two settings about voice note transcripts. */
void          contact_panel_set_transcript_prefs(ContactPanel *panel, const char *show, const char *transcribe, const char *languages);
/* And for TL;DR mode. */
void          contact_panel_set_tldr_pref(ContactPanel *panel, const char *tldr);
/* Whether this chat is your chat with the agent; "" for any chat but your own "message yourself" one, which hides the row. */
void          contact_panel_set_owner_pref(ContactPanel *panel, const char *owner_chat);
PopupResult   contact_panel_key(ContactPanel *panel, int is_key_code, int ch);
PopupResult   contact_panel_click(ContactPanel *panel, int y, int x);
void          contact_panel_wheel(ContactPanel *panel, int delta);
ContactAction contact_panel_choice(const ContactPanel *panel);
/* True when (y, x) is on the picture (to view it full size). */
int           contact_panel_hit_portrait(const ContactPanel *panel, int y, int x);
/* Draws the panel over `area`; returns 1 when *placement holds the picture as a pixel image. */
int           contact_panel_render(ContactPanel *panel, UiRect area, const Chat *chat, const ContactProfile *profile,
                                   const char *picture, const char *(*member_name)(void *ctx, const char *jid), void *names_ctx,
                                   ThumbnailCache *thumbs, int pixel_images, ImagePlacement *placement);

#endif
