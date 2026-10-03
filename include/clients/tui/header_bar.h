#ifndef APP_CLIENTS_TUI_HEADER_BAR_H
#define APP_CLIENTS_TUI_HEADER_BAR_H

#include "clients/tui/header_hits.h"
#include "clients/tui/header_model.h"
#include "clients/tui/ui_rect.h"

/* Draws the bar and records where + and your name went in `hits`. */
void header_bar_render(UiRect rect, const HeaderModel *model, HeaderHits *hits);
/* Click targets: the ☰ sidebar toggle and the ⚙ settings gear. */
int  header_bar_hit_menu(UiRect rect, int y, int x);
int  header_bar_hit_gear(UiRect rect, int y, int x);
/* The + that posts a status, and your name that opens your profile. */
int  header_bar_hit_post(const HeaderHits *hits, int y, int x);
int  header_bar_hit_profile(const HeaderHits *hits, int y, int x);
int header_bar_hit_account(const HeaderHits *hits, int y, int x);
/* The ⭕ that shows statuses. */
int  header_bar_hit_statuses(const HeaderHits *hits, int y, int x);
/* The 💬 Chats and 🤖 Agentic tabs. */
int  header_bar_hit_chats_tab(const HeaderHits *hits, int y, int x);
int  header_bar_hit_agents(const HeaderHits *hits, int y, int x);

#endif
