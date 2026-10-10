/* What agents may do in one chat, and what is hidden from what they read. */
#include "core/settings.h"
#include "engines/chat_agent_rules.h"
#include "engines/text_redactor.h"
#include "managers/automation_manager.h"
#include "resource_access/sqlite_chat_prefs_store.h"
#include "resource_access/sqlite_database.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); failures++; } } while (0)

static int becomes(const char *text, const char *want) {
    char *got = text_redactor_redact(text);
    int ok = want ? (got && strcmp(got, want) == 0) : got == NULL;
    if (!ok) printf("  \"%s\" gave \"%s\"\n", text, got ? got : "(unchanged)");
    free(got);
    return ok;
}

static void masking(void) {
    CHECK(becomes("Your verification code is 482913. Do not share it.", "Your verification code is [code]. Do not share it."),
          "a one-time code in a message that speaks of one is hidden");
    CHECK(becomes("OTP: 4829", "OTP: [code]") && becomes("Jou kode is 77120011", "Jou kode is [code]") && becomes("use PIN 123 456 to log in", "use PIN [code] to log in"),
          "short and long ones, in Afrikaans too, and one written as two groups of three");
    CHECK(becomes("See you at 1830, table for 6", NULL) && becomes("It cost 12500 last year", NULL),
          "a number in a message that speaks of no code is left alone");
    CHECK(becomes("The code is in the email from 2026-10-10 at 18:30", NULL) && becomes("code: call 0821234567 for it", NULL) &&
          becomes("Promo code SAVE2024 gives 12.50 off", NULL),
          "a date, a time, a phone number, a price and letters with digits are not codes");
    CHECK(becomes("Card 4111 1111 1111 1111 exp 12/27", "Card [card number] exp 12/27") && becomes("pay 4111-1111-1111-1111 now", "pay [card number] now") &&
          becomes("5500005555555559", "[card number]"),
          "a card number is hidden however it is spaced, with no word about a code needed");
    CHECK(becomes("Order 4111111111111112 shipped", NULL) && becomes("Tracking 123456789012345678901234", NULL),
          "sixteen digits that fail the card check, and a longer number, are left alone");
    CHECK(becomes("", NULL) && becomes(NULL, NULL) && becomes("no digits here, only a password", NULL), "and text with nothing in it stays as it is");
    CHECK(becomes("code 1234 and card 4111111111111111", "code [code] and card [card number]"), "both kinds in one message");
}

static void rules(sqlite3 *db) {
    CHECK(chat_agent_rules_readable(CHAT_AGENT_FOLLOW) && chat_agent_rules_readable(CHAT_AGENT_NO_SEND) && !chat_agent_rules_readable(CHAT_AGENT_HIDDEN),
          "a hidden chat cannot be read; a read-only one can");
    CHECK(chat_agent_rules_writable(CHAT_AGENT_ALWAYS_ASK) && !chat_agent_rules_writable(CHAT_AGENT_NO_SEND) && !chat_agent_rules_writable(CHAT_AGENT_HIDDEN),
          "read only and hidden take no writes");
    CHECK(chat_agent_rules_always_asks(CHAT_AGENT_ALWAYS_ASK) && !chat_agent_rules_always_asks(CHAT_AGENT_FOLLOW), "always ask asks always");
    ChatAgentRule r = CHAT_AGENT_FOLLOW;
    int seen = 0;
    for (int i = 0; i < 4; i++) { r = chat_agent_rules_next(r); seen |= 1 << (int)r; }
    CHECK(seen == 15 && r == CHAT_AGENT_FOLLOW, "stepping goes through all four and comes back");
    CHECK(chat_agent_rules_from(99) == CHAT_AGENT_FOLLOW && chat_agent_rules_from(3) == CHAT_AGENT_HIDDEN, "an unknown stored value is the default");

    IChatPrefsStore *prefs = sqlite_chat_prefs_store_create(db);
    Settings settings;
    settings_set_defaults(&settings);
    AutomationManagerDeps deps = { NULL, &settings, NULL, sqlite_chat_prefs_store_agents(prefs) };
    AutomationManager *mgr = automation_manager_create(&deps);
    const char *mom = "27820000001@s.whatsapp.net", *work = "27820000002@s.whatsapp.net";
    CHECK(automation_manager_chat_rule(mgr, mom) == CHAT_AGENT_FOLLOW, "a chat starts with no rule of its own");
    CHECK(automation_manager_set_chat_rule(mgr, mom, CHAT_AGENT_HIDDEN) == 0 && automation_manager_chat_rule(mgr, mom) == CHAT_AGENT_HIDDEN &&
          automation_manager_chat_rule(mgr, work) == CHAT_AGENT_FOLLOW, "a rule is kept for its chat alone");
    Chat chats[2];
    chat_init(&chats[0], mom);
    str_copy(chats[0].name, sizeof(chats[0].name), "Mom");
    chat_init(&chats[1], work);
    str_copy(chats[1].name, sizeof(chats[1].name), "Work");
    CHECK(!automation_manager_chat_allowed(mgr, &chats[0]) && automation_manager_chat_allowed(mgr, &chats[1]), "a hidden chat is not one agents may see");
    int found = -1, count = 0, candidates[4];
    CHECK(automation_manager_resolve(mgr, chats, 2, "Mom", &found, candidates, 4, &count) == CHAT_RESOLUTION_NOT_FOUND &&
          automation_manager_resolve(mgr, chats, 2, mom, &found, candidates, 4, &count) == CHAT_RESOLUTION_NOT_FOUND,
          "and cannot be named, by its name or its number");
    CHECK(automation_manager_resolve(mgr, chats, 2, "Work", &found, candidates, 4, &count) == CHAT_RESOLUTION_FOUND && found == 1,
          "another chat is found where it really is in the list");
    automation_manager_destroy(mgr);
    mgr = automation_manager_create(&deps);
    CHECK(automation_manager_chat_rule(mgr, mom) == CHAT_AGENT_HIDDEN, "the rule is still there when tawk starts again");
    ChatPrefs kept;
    prefs->get(prefs, mom, &kept);
    CHECK(kept.agent_rule == CHAT_AGENT_HIDDEN, "and reads back with the chat's other choices");
    settings.automation_mask_codes = 1;
    CHECK(automation_manager_masks(mgr, CONTROL_ORIGIN_MCP) && !automation_manager_masks(mgr, CONTROL_ORIGIN_CLI), "an agent for a model reads masked text; your own shell commands do not");
    settings.automation_mask_codes = 0;
    CHECK(!automation_manager_masks(mgr, CONTROL_ORIGIN_MCP), "and nobody does once you switch it off");
    automation_manager_destroy(mgr);
    prefs->destroy(prefs);
}

int main(void) {
    char db_path[256];
    snprintf(db_path, sizeof(db_path), "/tmp/tawk_agent_rules_test_%d.db", (int)getpid());
    remove(db_path);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) return 1;
    masking();
    rules(db);
    sqlite_database_close(db);
    remove(db_path);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("ok: rules for agents per chat, and masking\n");
    return 0;
}
