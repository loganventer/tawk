/* Two linked numbers side by side, end to end through the real account host
 * and managers on a temporary database, with a gateway that plays WhatsApp:
 * each account shows its own QR code, is linked to its own number, receives
 * its own messages and keeps them apart from the other's; the same person on
 * both shows as one chat and one conversation; a message goes out through
 * the account chosen for it and no other; and logging one out or removing it
 * leaves the other as it was. */
#include "clients/tui/merged_message_window.h"
#include "clients/tui/unified_chat_list.h"
#include "composition/account_host.h"
#include "engines/reply_account_policy.h"
#include "managers/account_roster_manager.h"
#include "managers/messaging_manager.h"
#include "managers/settings_manager.h"
#include "resource_access/ini_settings_store.h"
#include "resource_access/json_theme_repository.h"
#include "resource_access/sqlite_account_store.h"
#include "resource_access/sqlite_chat_prefs_store.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/text_chat_exporter.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define MOM      "27820000001@s.whatsapp.net"
#define BOSS     "27820000005@s.whatsapp.net"
#define MAIN_JID "27830000000@s.whatsapp.net"
#define WORK_JID "27830000001@s.whatsapp.net"

/* ---- WhatsApp, played by the test ------------------------------------------ */

typedef struct Phone {
    IMessageGateway gateway;
    AccountId       account;
    EventQueue     *events;
    int             started, logged_out;
    int             texts;
    char            last_to[128];
    char            last_text[256];
} Phone;

#define PHONES 4
static Phone phones[PHONES];
static int phone_count;

static Phone *phone_of(AccountId account) {
    for (int i = 0; i < phone_count; i++) if (phones[i].account == account) return &phones[i];
    return NULL;
}

/* A gateway that has no login yet asks to be linked, as the real ones do. */
static int ph_start(IMessageGateway *self) {
    Phone *p = self->ctx;
    p->started = 1;
    Event e;
    event_init(&e, EVENT_AUTH_QR);
    char qr[64];
    snprintf(qr, sizeof(qr), "QR-FOR-ACCOUNT-%d", p->account);
    e.qr_ascii = str_dup(qr);
    event_queue_push(p->events, &e);
    return 0;
}
static int ph_ok(IMessageGateway *self) { (void)self; return 0; }
static void ph_stop(IMessageGateway *self) { (void)self; }
static int ph_text(IMessageGateway *self, const char *jid, const OutgoingText *t, const char *id) {
    (void)id;
    Phone *p = self->ctx;
    p->texts++;
    str_copy(p->last_to, sizeof(p->last_to), jid);
    str_copy(p->last_text, sizeof(p->last_text), t->text);
    return 0;
}
static int ph_jid(IMessageGateway *self, const char *jid) { (void)self; (void)jid; return 0; }
static int ph_picture(IMessageGateway *self, const char *jid, int full) { (void)self; (void)jid; (void)full; return 0; }
static int ph_typing(IMessageGateway *self, const char *jid, const char *st) { (void)self; (void)jid; (void)st; return 0; }
static int ph_presence(IMessageGateway *self, int on) { (void)self; (void)on; return 0; }
static int ph_read(IMessageGateway *self, const ReadRequest *r) { (void)self; (void)r; return 0; }
static int ph_logout(IMessageGateway *self) {
    Phone *p = self->ctx;
    p->logged_out = 1;
    Event e;
    event_init(&e, EVENT_AUTH_LOGGED_OUT);
    event_queue_push(p->events, &e);
    return 0;
}
static void ph_destroy(IMessageGateway *self) { Phone *p = self->ctx; p->started = 0; p->events = NULL; }

static int factory_create(IGatewayFactory *self, AccountId account, EventQueue *events, GatewayParts *out) {
    (void)self;
    if (phone_count >= PHONES) return -1;
    Phone *p = &phones[phone_count++];
    memset(p, 0, sizeof(*p));
    p->account = account;
    p->events = events;
    p->gateway.ctx = p;
    p->gateway.start = ph_start;
    p->gateway.connect = ph_ok;
    p->gateway.reconnect = ph_ok;
    p->gateway.stop = ph_stop;
    p->gateway.send_text = ph_text;
    p->gateway.subscribe = ph_jid;
    p->gateway.request_profile = ph_jid;
    p->gateway.request_picture = ph_picture;
    p->gateway.typing = ph_typing;
    p->gateway.presence = ph_presence;
    p->gateway.mark_read = ph_read;
    p->gateway.request_qr = ph_ok;
    p->gateway.logout = ph_logout;
    p->gateway.destroy = ph_destroy;
    memset(out, 0, sizeof(*out));
    out->gateway = &p->gateway;
    out->backend_name = "test";
    return 0;
}
static void factory_destroy(IGatewayFactory *self) { (void)self; }
static IGatewayFactory factory = { NULL, factory_create, factory_destroy };

/* The phone scans the code: the account is linked to `jid`. */
static void scan(AccountId account, const char *jid) {
    Phone *p = phone_of(account);
    Event e;
    event_init(&e, EVENT_AUTH_CONNECTED);
    str_copy(e.jid, sizeof(e.jid), jid);
    str_copy(e.name, sizeof(e.name), "Logan");
    event_queue_push(p->events, &e);
}

/* Someone writes to one of your numbers. */
static void arrives(AccountId account, const char *id, const char *from, const char *name, const char *text, int64_t ts) {
    Phone *p = phone_of(account);
    Event chat;
    event_init(&chat, EVENT_CHAT_UPDATE);
    chat_init(&chat.chat, from);
    str_copy(chat.chat.name, sizeof(chat.chat.name), name);
    chat.chat.last_ts = ts;
    event_queue_push(p->events, &chat);
    Event e;
    event_init(&e, EVENT_MESSAGE_UPSERT);
    str_copy(e.message.id, sizeof(e.message.id), id);
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), from);
    str_copy(e.message.sender_jid, sizeof(e.message.sender_jid), from);
    e.message.timestamp = ts;
    message_set_text(&e.message, text);
    e.live = 1;
    event_queue_push(p->events, &e);
}

/* ---- the world under test ---------------------------------------------------- */

static void fake_notify(INotifier *self, const Notification *n) { (void)self; (void)n; }
static INotifier notifier = { NULL, fake_notify, NULL };

static sqlite3 *db;
static IAccountDirectory *directory;
static AccountRosterManager *roster;
static AccountId work;

static void tick(void) {
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < directory->count(directory); i++) {
            ManagerChanges ch;
            messaging_manager_tick(directory->at(directory, i)->messaging, &ch);
        }
    }
}

static MessagingManager *messaging(AccountId id) {
    const AccountServices *sv = directory->find(directory, id);
    return sv ? sv->messaging : NULL;
}

static int rows(const char *table, AccountId account) {
    char sql[160];
    snprintf(sql, sizeof(sql), "SELECT count(*) FROM %s WHERE account_id = %d", table, account);
    sqlite3_stmt *st = NULL;
    int n = -1;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

static void no_prefs(void *ctx, const char *jid, ChatPrefs *out) {
    memset(out, 0, sizeof(*out));
    str_copy(out->jid, sizeof(out->jid), jid);
    if (ctx) out->merge = *(ChatMergeChoice *)ctx;
}

/* The one chat list, as the terminal client builds it. Returns how many rows it has. */
static int listed(int merge_setting, ChatMergeChoice *choice, UnifiedChatList *list) {
    ChatSource sources[ACCOUNT_MAX];
    int n = 0;
    for (int i = 0; i < directory->count(directory); i++) {
        const AccountServices *sv = directory->at(directory, i);
        sources[n].account = sv->id;
        sources[n].chats = messaging_manager_chats(sv->messaging, &sources[n].count);
        n++;
    }
    UnifiedChatRules rules = { ACCOUNT_ID_NONE, merge_setting, account_roster_manager_primary(roster), no_prefs, choice };
    unified_chat_list_build(list, sources, n, &rules);
    return list->count;
}

/* ---- the checks ------------------------------------------------------------- */

static void test_each_is_linked_by_itself(void) {
    CHECK(directory->count(directory) == 2 && phone_count == 2, "both accounts are started, each with a connection of its own");
    messaging_manager_start(messaging(ACCOUNT_ID_FIRST));
    messaging_manager_start(messaging(work));
    tick();
    CHECK(messaging_manager_auth_state(messaging(ACCOUNT_ID_FIRST)) == AUTH_STATE_NEEDS_LOGIN &&
          messaging_manager_auth_state(messaging(work)) == AUTH_STATE_NEEDS_LOGIN, "each waits to be linked");

    scan(ACCOUNT_ID_FIRST, MAIN_JID);
    tick();
    CHECK(messaging_manager_auth_state(messaging(ACCOUNT_ID_FIRST)) == AUTH_STATE_CONNECTED &&
          messaging_manager_auth_state(messaging(work)) == AUTH_STATE_NEEDS_LOGIN, "linking one leaves the other waiting");
    scan(work, WORK_JID);
    tick();
    CHECK(messaging_manager_auth_state(messaging(work)) == AUTH_STATE_CONNECTED, "then the other is linked too");
    CHECK(!strcmp(messaging_manager_user_jid(messaging(ACCOUNT_ID_FIRST)), MAIN_JID) &&
          !strcmp(messaging_manager_user_jid(messaging(work)), WORK_JID), "and each knows its own number");
}

static void test_messages_stay_with_their_account(void) {
    arrives(ACCOUNT_ID_FIRST, "M1", MOM, "Mom", "supper on sunday?", 1790000100);
    arrives(work, "W1", BOSS, "Boss", "meeting at 3", 1790000200);
    arrives(work, "W2", MOM, "Mom", "are you at work?", 1790000300);
    tick();
    int n1 = 0, n2 = 0;
    messaging_manager_chats(messaging(ACCOUNT_ID_FIRST), &n1);
    messaging_manager_chats(messaging(work), &n2);
    CHECK(n1 == 1 && n2 == 2, "each account lists the chats that reached it");
    CHECK(rows("messages", ACCOUNT_ID_FIRST) == 1 && rows("messages", work) == 2, "and its messages are stored under its own id");
    CHECK(rows("chats", ACCOUNT_ID_FIRST) == 1 && rows("chats", work) == 2, "as are its chats");
}

static void test_one_person_one_chat(void) {
    UnifiedChatList list;
    unified_chat_list_init(&list);
    CHECK(listed(1, NULL, &list) == 2, "with merging on, someone on both numbers is one row beside the other chats");
    const Chat *mom = unified_chat_list_find(&list, ACCOUNT_ID_NONE, MOM);
    if (!mom) mom = unified_chat_list_find(&list, work, MOM);
    if (!mom) mom = unified_chat_list_find(&list, ACCOUNT_ID_FIRST, MOM);
    CHECK(mom && (mom->accounts & 3u) == 3u, "and the row stands for both accounts");
    CHECK(listed(0, NULL, &list) == 3, "with merging off they are two rows");
    ChatMergeChoice never = CHAT_MERGE_NEVER, always = CHAT_MERGE_ALWAYS;
    CHECK(listed(1, &never, &list) == 3, "a contact set apart stays two rows whatever the setting");
    CHECK(listed(0, &always, &list) == 2, "and one set to merge is one row whatever the setting");
    unified_chat_list_free(&list);

    /* The conversation: both accounts' messages with her, in the order they happened. */
    messaging_manager_open_chat(messaging(ACCOUNT_ID_FIRST), MOM);
    messaging_manager_open_chat(messaging(work), MOM);
    MessageSource sources[2];
    sources[0].account = ACCOUNT_ID_FIRST;
    sources[0].messages = messaging_manager_messages(messaging(ACCOUNT_ID_FIRST), &sources[0].count);
    sources[1].account = work;
    sources[1].messages = messaging_manager_messages(messaging(work), &sources[1].count);
    MergedMessageWindow window;
    merged_message_window_init(&window);
    merged_message_window_build(&window, sources, 2, ACCOUNT_ID_FIRST);
    CHECK(window.count == 2 && !strcmp(window.items[0].id, "M1") && !strcmp(window.items[1].id, "W2"),
          "opened, it is one conversation in the order things were said");
    CHECK(window.count == 2 && window.owners[0] == ACCOUNT_ID_FIRST && window.owners[1] == work, "with each message knowing its account");
    merged_message_window_free(&window);
}

static void test_sent_from_the_chosen_number(void) {
    Phone *main_phone = phone_of(ACCOUNT_ID_FIRST), *work_phone = phone_of(work);
    ReplyAccountCandidate both[2] = { { ACCOUNT_ID_FIRST, 1, 1790000100 }, { work, 1, 1790000300 } };

    /* Nothing chosen: the primary account answers. */
    AccountId from = reply_account_policy_choose(ACCOUNT_ID_NONE, ACCOUNT_ID_NONE, account_roster_manager_primary(roster), both, 2);
    CHECK(from == ACCOUNT_ID_FIRST, "with nothing chosen a message goes from the primary account");
    OutgoingText text;
    memset(&text, 0, sizeof(text));
    text.text = "yes, see you sunday";
    CHECK(messaging_manager_send_text_to(messaging(from), MOM, &text) == 0, "it is sent");
    tick();
    CHECK(main_phone->texts == 1 && work_phone->texts == 0 && !strcmp(main_phone->last_to, MOM),
          "through that number and not the other");

    /* The contact's own sending number wins over the primary. */
    CHECK(account_roster_manager_set_send_account(roster, MOM, work) == 0, "a sending number is kept for a contact");
    ChatPrefs kept;
    account_roster_manager_chat_prefs(roster, MOM, &kept);
    from = reply_account_policy_choose(ACCOUNT_ID_NONE, kept.send_account, account_roster_manager_primary(roster), both, 2);
    CHECK(from == work, "and is the one her next message goes from");
    text.text = "at work, yes";
    messaging_manager_send_text_to(messaging(from), MOM, &text);
    tick();
    CHECK(work_phone->texts == 1 && main_phone->texts == 1 && !strcmp(work_phone->last_text, "at work, yes"),
          "through that number and not the other");

    /* The number picked for one message wins over both. */
    from = reply_account_policy_choose(ACCOUNT_ID_FIRST, kept.send_account, account_roster_manager_primary(roster), both, 2);
    CHECK(from == ACCOUNT_ID_FIRST, "a number picked for one message wins for that message");

    /* An account with no chat with someone cannot be the one that answers them. */
    ReplyAccountCandidate boss[2] = { { ACCOUNT_ID_FIRST, 0, 0 }, { work, 1, 1790000200 } };
    CHECK(reply_account_policy_choose(ACCOUNT_ID_NONE, ACCOUNT_ID_NONE, ACCOUNT_ID_FIRST, boss, 2) == work,
          "someone who only ever wrote to one number is answered from that number");

    CHECK(rows("messages", ACCOUNT_ID_FIRST) == 2 && rows("messages", work) == 3, "what you sent is stored with the account that sent it");
}

static void test_one_goes_the_other_stays(void) {
    Phone *work_phone = phone_of(work);
    messaging_manager_logout(messaging(work));
    tick();
    CHECK(work_phone->logged_out && messaging_manager_auth_state(messaging(work)) == AUTH_STATE_NEEDS_LOGIN, "logging one account out unlinks it");
    CHECK(messaging_manager_auth_state(messaging(ACCOUNT_ID_FIRST)) == AUTH_STATE_CONNECTED, "and leaves the other connected");

    arrives(ACCOUNT_ID_FIRST, "M2", MOM, "Mom", "bring bread", 1790000400);
    tick();
    CHECK(rows("messages", ACCOUNT_ID_FIRST) == 3, "which goes on receiving");

    CHECK(directory->forget(directory, work) == 0 && directory->count(directory) == 1 && directory->find(directory, work) == NULL,
          "removing an account stops it");
    CHECK(account_roster_manager_remove(roster, work) == 0 && rows("messages", work) == 0 && rows("chats", work) == 0,
          "and takes its chats and messages with it");
    CHECK(rows("messages", ACCOUNT_ID_FIRST) == 3 && rows("chats", ACCOUNT_ID_FIRST) == 1, "while the other account's are untouched");
}

/* One log for every account: a line written while an account is worked on says which. */
static void test_log_says_which_account(const char *dir) {
    char path[600], text[2000] = "";
    snprintf(path, sizeof(path), "%s/tawk.log", dir);
    log_open(path, LOG_LEVEL_INFO);
    log_context_set("work");
    LOG_INFO("reconnecting");
    char held[64];
    log_context_get(held, sizeof(held));
    log_context_set(NULL);
    LOG_INFO("settings saved");
    log_context_set(held);
    LOG_INFO("connected");
    log_context_set(NULL);
    log_close();
    FILE *f = fopen(path, "r");
    if (f) { size_t n = fread(text, 1, sizeof(text) - 1, f); text[n] = '\0'; fclose(f); }
    CHECK(strstr(text, "[INFO] [work] reconnecting") && strstr(text, "[INFO] settings saved") && strstr(text, "[INFO] [work] connected"),
          "a log line carries the label of the account it is about, and a line about none carries nothing");
}

int main(void) {
    char dir[] = "/tmp/tawk-two-accounts-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char db_path[600], config[600];
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", dir);
    snprintf(config, sizeof(config), "%s/config.ini", dir);
    db = sqlite_database_open(db_path, NULL);
    if (!db) return 1;

    ISettingsStore *store = ini_settings_store_create(config);
    IThemeRepository *themes = json_theme_repository_create("themes", dir);
    SettingsManager *settings_mgr = settings_manager_create(store, themes);
    settings_manager_load(settings_mgr);
    Settings s = *settings_manager_current(settings_mgr);
    str_copy(s.data_dir, sizeof(s.data_dir), dir);
    snprintf(s.media_dir, sizeof(s.media_dir), "%s/media", dir);
    settings_manager_apply(settings_mgr, &s);

    IAccountStore *account_store = sqlite_account_store_create(db);
    IChatPrefsStore *prefs = sqlite_chat_prefs_store_create(db);
    AccountRosterManagerDeps roster_deps = { account_store, prefs };
    roster = account_roster_manager_create(&roster_deps);
    account_roster_manager_add(roster, "work", 1, &work);

    IChatExporter *exporter = text_chat_exporter_create();
    AccountRuntimeParams params = { db, settings_manager_current(settings_mgr), &notifier, exporter, &factory };
    AccountHost *host = account_host_create(&params, account_store);
    if (!host) { fprintf(stderr, "FAIL: the accounts could not be started\n"); return 1; }
    directory = account_host_directory(host);

    test_each_is_linked_by_itself();
    test_messages_stay_with_their_account();
    test_one_person_one_chat();
    test_sent_from_the_chosen_number();
    test_one_goes_the_other_stays();
    test_log_says_which_account(dir);

    account_host_destroy(host);
    exporter->destroy(exporter);
    account_roster_manager_destroy(roster);
    prefs->destroy(prefs);
    account_store->destroy(account_store);
    sqlite_database_close(db);
    settings_manager_destroy(settings_mgr);
    themes->destroy(themes);
    store->destroy(store);
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);
    if (failures == 0) printf("ok: two linked accounts run side by side, each with its own login, chats and messages; one person on both is one conversation; a message goes out through the number chosen for it\n");
    return failures != 0;
}
