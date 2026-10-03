/* The control client with several accounts, over a fake socket and the real
 * managers on a temporary database: an agent sees only the accounts that are
 * open to it, a request is served by the account it names or by the default
 * one, each account's own access level decides what may be done there, a
 * send goes out through the right account, and events say whose they are. */
#include "clients/control/control_server.h"
#include "clients/tui/approval_queue.h"
#include "cJSON.h"
#include "managers/account_roster_manager.h"
#include "managers/automation_manager.h"
#include "managers/messaging_manager.h"
#include "managers/scheduling_manager.h"
#include "managers/settings_manager.h"
#include "resource_access/ini_settings_store.h"
#include "resource_access/json_theme_repository.h"
#include "resource_access/sqlite_account_store.h"
#include "resource_access/sqlite_automation_log.h"
#include "resource_access/sqlite_chat_prefs_store.h"
#include "resource_access/sqlite_chat_store.h"
#include "resource_access/sqlite_contact_store.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_jid_alias_store.h"
#include "resource_access/sqlite_message_store.h"
#include "resource_access/sqlite_reaction_store.h"
#include "resource_access/sqlite_receipt_store.h"
#include "resource_access/sqlite_scheduled_message_store.h"
#include "resource_access/text_chat_exporter.h"
#include "utilities/event_queue.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define MOM   "27820000001@s.whatsapp.net"
#define BOSS  "27820000005@s.whatsapp.net"
#define WORLD 3                       /* accounts in the test: main, work and one closed to agents */

/* ---- a socket that is only memory ----------------------------------------- */

#define MAX_LINES 256

static ControlInbound inbox[64];
static int inbox_count;
static char *outbox[MAX_LINES];
static int outbox_count;

static int ft_listen(IControlTransport *self, const char *path, char *why, unsigned long size) { (void)self; (void)path; (void)why; (void)size; return 0; }
static void ft_stop(IControlTransport *self) { (void)self; }
static int ft_poll(IControlTransport *self, ControlInbound *out, int max) {
    (void)self;
    int n = inbox_count < max ? inbox_count : max;
    memcpy(out, inbox, (size_t)n * sizeof(*out));
    memmove(inbox, inbox + n, (size_t)(inbox_count - n) * sizeof(*inbox));
    inbox_count -= n;
    return n;
}
static int ft_send(IControlTransport *self, int conn, const char *line) {
    (void)self; (void)conn;
    if (outbox_count < MAX_LINES) outbox[outbox_count++] = str_dup(line);
    return 0;
}
static void ft_close(IControlTransport *self, int conn) { (void)self; (void)conn; }

static void clear_outbox(void) {
    for (int i = 0; i < outbox_count; i++) free(outbox[i]);
    outbox_count = 0;
}

/* ---- one account: a gateway that counts what it sent, and the managers over it ---- */

typedef struct World {
    AccountId         id;
    IMessageGateway   gateway;
    int               texts;                 /* messages this account's gateway was asked to send */
    char              last_text[256];
    EventQueue       *events;
    IMessageStore    *messages;
    IChatStore       *chats;
    IContactStore    *contacts;
    IJidAliasStore   *aliases;
    IReactionStore   *reactions;
    IReceiptStore    *receipts;
    IScheduledMessageStore *scheduled;
    MessagingManager *messaging;
    SchedulingManager *scheduling;
    AccountServices   services;
} World;

static World worlds[WORLD];

static int gw_ok(IMessageGateway *self) { (void)self; return 0; }
static int gw_text(IMessageGateway *self, const char *jid, const OutgoingText *t, const char *id) {
    (void)jid; (void)id;
    World *w = self->ctx;
    w->texts++;
    str_copy(w->last_text, sizeof(w->last_text), t->text);
    return 0;
}
static int gw_jid(IMessageGateway *self, const char *jid) { (void)self; (void)jid; return 0; }
static int gw_picture(IMessageGateway *self, const char *jid, int full) { (void)self; (void)jid; (void)full; return 0; }
static int gw_typing(IMessageGateway *self, const char *jid, const char *st) { (void)self; (void)jid; (void)st; return 0; }
static int gw_presence(IMessageGateway *self, int on) { (void)self; (void)on; return 0; }
static int gw_read(IMessageGateway *self, const ReadRequest *r) { (void)self; (void)r; return 0; }
static void fake_notify(INotifier *self, const Notification *n) { (void)self; (void)n; }
static INotifier notifier = { NULL, fake_notify, NULL };

static char admin_token[128];
static int at_save(IAdminTokenStore *self, const char *token) { (void)self; str_copy(admin_token, sizeof(admin_token), token); return 0; }
static void at_remove(IAdminTokenStore *self) { (void)self; admin_token[0] = '\0'; }
static void at_destroy(IAdminTokenStore *self) { (void)self; }
static IAdminTokenStore admin_tokens = { NULL, at_save, at_remove, at_destroy };

/* ---- the directory the control client is handed ----------------------------- */

static int dir_count(IAccountDirectory *self) { (void)self; return WORLD; }
static const AccountServices *dir_at(IAccountDirectory *self, int i) { (void)self; return i >= 0 && i < WORLD ? &worlds[i].services : NULL; }
static const AccountServices *dir_find(IAccountDirectory *self, AccountId id) {
    (void)self;
    for (int i = 0; i < WORLD; i++) if (worlds[i].id == id) return &worlds[i].services;
    return NULL;
}
static const AccountServices *dir_start(IAccountDirectory *self, AccountId id) { return dir_find(self, id); }
static int dir_stop(IAccountDirectory *self, AccountId id) { (void)self; (void)id; return -1; }
static void dir_relabel(IAccountDirectory *self, AccountId id) { (void)self; (void)id; }
static IAccountDirectory directory = { NULL, dir_count, dir_at, dir_find, dir_start, dir_stop, dir_stop, dir_relabel };

/* ---- the world under test ---------------------------------------------------- */

static IControlTransport transport = { NULL, ft_listen, ft_stop, ft_poll, ft_send, ft_close, NULL };
static SettingsManager *settings_mgr;
static AccountRosterManager *roster;
static AutomationManager *automation;
static ApprovalQueue *queue;
static ControlServer *server;

static void tick(void) {
    for (int i = 0; i < WORLD; i++) {
        ManagerChanges ch;
        messaging_manager_tick(worlds[i].messaging, &ch);
    }
    control_server_tick(server);
}

static void make_world(World *w, sqlite3 *db, AccountId id, IChatExporter *exporter, const char *own_jid) {
    memset(w, 0, sizeof(*w));
    w->id = id;
    w->gateway.ctx = w;
    w->gateway.start = gw_ok;
    w->gateway.connect = gw_ok;
    w->gateway.send_text = gw_text;
    w->gateway.subscribe = gw_jid;
    w->gateway.request_profile = gw_jid;
    w->gateway.request_picture = gw_picture;
    w->gateway.typing = gw_typing;
    w->gateway.presence = gw_presence;
    w->gateway.mark_read = gw_read;
    w->events = event_queue_create(64);
    w->messages = sqlite_message_store_create(db, id);
    w->chats = sqlite_chat_store_create(db, id);
    w->contacts = sqlite_contact_store_create(db, id);
    w->aliases = sqlite_jid_alias_store_create(db, id);
    w->reactions = sqlite_reaction_store_create(db, id);
    w->receipts = sqlite_receipt_store_create(db, id);
    w->scheduled = sqlite_scheduled_message_store_create(db, id);
    MessagingManagerDeps deps = { &w->gateway, w->messages, w->chats, w->contacts, w->aliases, w->reactions, w->receipts, &notifier,
                                  w->events, settings_manager_current(settings_mgr), NULL, exporter, NULL, NULL, id, NULL };
    w->messaging = messaging_manager_create(&deps);
    SchedulingManagerDeps sched = { w->scheduled };
    w->scheduling = scheduling_manager_create(&sched);
    w->services = (AccountServices){ id, "", "test", w->messaging, NULL, NULL, NULL, NULL, NULL, w->scheduling };
    messaging_manager_start(w->messaging);
    Event connected;
    event_init(&connected, EVENT_AUTH_CONNECTED);
    str_copy(connected.jid, sizeof(connected.jid), own_jid);
    str_copy(connected.name, sizeof(connected.name), "Logan");
    event_queue_push(w->events, &connected);
}

static void add_chat(World *w, const char *jid, const char *name) {
    Chat c;
    chat_init(&c, jid);
    str_copy(c.name, sizeof(c.name), name);
    c.is_locked = 0;
    c.is_archived = 0;
    c.last_ts = 1790000000;
    w->chats->upsert(w->chats, &c);
}

static void incoming(World *w, const char *id, const char *jid, const char *text) {
    Event e;
    event_init(&e, EVENT_MESSAGE_UPSERT);
    str_copy(e.message.id, sizeof(e.message.id), id);
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), jid);
    str_copy(e.message.sender_jid, sizeof(e.message.sender_jid), jid);
    e.message.timestamp = 1790000500;
    message_set_text(&e.message, text);
    e.live = 1;
    event_queue_push(w->events, &e);
}

static int next_conn = 1;

static int open_client(void) {
    int conn = next_conn++;
    inbox[inbox_count++] = (ControlInbound){ CONTROL_INBOUND_OPENED, conn, NULL };
    inbox[inbox_count++] = (ControlInbound){ CONTROL_INBOUND_LINE, conn,
        str_dup("{\"id\":\"h\",\"op\":\"hello\",\"args\":{\"client\":\"test\",\"protocol\":1,\"origin\":\"mcp\"}}") };
    tick();
    return conn;
}

static void say(int conn, const char *line) {
    inbox[inbox_count++] = (ControlInbound){ CONTROL_INBOUND_LINE, conn, str_dup(line) };
    tick();
}

/* The answer to `id` among what was sent, parsed (the caller deletes it), or NULL. */
static cJSON *reply(const char *id) {
    for (int i = outbox_count - 1; i >= 0; i--) {
        cJSON *o = cJSON_Parse(outbox[i]);
        const cJSON *got = cJSON_GetObjectItemCaseSensitive(o, "id");
        if (cJSON_IsString(got) && !strcmp(got->valuestring, id) && !cJSON_GetObjectItemCaseSensitive(o, "evt")) return o;
        cJSON_Delete(o);
    }
    return NULL;
}

static const char *error_code(const cJSON *r) {
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "error"), "code");
    return cJSON_IsString(code) ? code->valuestring : "";
}

static const cJSON *result(const cJSON *r) { return cJSON_GetObjectItemCaseSensitive(r, "result"); }

static int ok(const cJSON *r) { return r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")); }

/* How many chats an answer to list_chats holds, or -1. */
static int chat_count(const cJSON *r) {
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(result(r), "chats");
    return cJSON_IsArray(list) ? cJSON_GetArraySize(list) : -1;
}

/* The first event named `name` that was sent, parsed (the caller deletes it), or NULL. */
static cJSON *event(const char *name) {
    for (int i = 0; i < outbox_count; i++) {
        cJSON *o = cJSON_Parse(outbox[i]);
        const cJSON *evt = cJSON_GetObjectItemCaseSensitive(o, "evt");
        if (cJSON_IsString(evt) && !strcmp(evt->valuestring, name)) return o;
        cJSON_Delete(o);
    }
    return NULL;
}

static int account_id_of(const cJSON *object) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(object, "account"), "id");
    return cJSON_IsNumber(id) ? id->valueint : -1;
}

static void answer_first(int approved) {
    const ApprovalRequest *r = approval_queue_at(queue, 0);
    if (r) approval_queue_answer(queue, r->id, approved, NULL, 0);
    tick();
}

/* ---- the checks ------------------------------------------------------------- */

static void test_who_is_seen(AccountId work, AccountId closed) {
    int conn = open_client();
    cJSON *h = reply("h");
    const cJSON *r = result(h);
    const cJSON *accounts = cJSON_GetObjectItemCaseSensitive(r, "accounts");
    CHECK(ok(h) && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "multi_account")), "hello says this tawk serves several accounts");
    CHECK(cJSON_IsArray(accounts) && cJSON_GetArraySize(accounts) == 2, "it lists the accounts open to agents, and not the one that is closed");
    const cJSON *def = cJSON_GetObjectItemCaseSensitive(r, "default_account");
    CHECK(cJSON_IsNumber(def) && def->valueint == ACCOUNT_ID_FIRST, "the primary account is the default");
    const cJSON *second = cJSON_GetArrayItem(accounts, 1);
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(second, "label"), *access = cJSON_GetObjectItemCaseSensitive(second, "access");
    CHECK(cJSON_IsString(label) && !strcmp(label->valuestring, "work") && cJSON_IsString(access) && !strcmp(access->valuestring, "read"),
          "each account comes with its label and its own access level");
    cJSON_Delete(h);
    clear_outbox();

    say(conn, "{\"id\":\"l\",\"op\":\"list_accounts\"}");
    cJSON *l = reply("l");
    CHECK(ok(l) && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(result(l), "accounts")) == 2, "list_accounts gives the same two");
    cJSON_Delete(l);

    char line[160];
    snprintf(line, sizeof(line), "{\"id\":\"c\",\"op\":\"list_chats\",\"args\":{\"account\":%d}}", closed);
    say(conn, line);
    cJSON *c = reply("c");
    CHECK(c && !ok(c) && !strcmp(error_code(c), "not_found"), "naming an account closed to agents reads as naming one that does not exist");
    cJSON_Delete(c);
    say(conn, "{\"id\":\"n\",\"op\":\"list_chats\",\"args\":{\"account\":\"nobody\"}}");
    cJSON *n = reply("n");
    CHECK(n && !ok(n) && !strcmp(error_code(n), "not_found"), "and so does a label no account has");
    cJSON_Delete(n);
    (void)work;
    clear_outbox();
}

static void test_served_by_the_named_account(AccountId work) {
    int conn = open_client();
    clear_outbox();
    say(conn, "{\"id\":\"a\",\"op\":\"list_chats\"}");
    cJSON *a = reply("a");
    CHECK(ok(a) && chat_count(a) == 1, "a request that names no account is served by the default one");
    cJSON_Delete(a);

    char line[200];
    snprintf(line, sizeof(line), "{\"id\":\"b\",\"op\":\"list_chats\",\"args\":{\"account\":%d}}", work);
    say(conn, line);
    cJSON *b = reply("b");
    CHECK(ok(b) && chat_count(b) == 2, "one that names an account by id is served by it");
    cJSON_Delete(b);
    say(conn, "{\"id\":\"w\",\"op\":\"list_chats\",\"args\":{\"account\":\"Work\"}}");
    cJSON *w = reply("w");
    CHECK(ok(w) && chat_count(w) == 2, "or by its label, whatever its case");
    cJSON_Delete(w);

    /* The same contact is a chat of both accounts; a message is read from the one that is named. */
    say(conn, "{\"id\":\"r1\",\"op\":\"read_messages\",\"args\":{\"chat\":\"" MOM "\"}}");
    cJSON *r1 = reply("r1");
    CHECK(ok(r1) && strstr(outbox[outbox_count - 1], "from main") && !strstr(outbox[outbox_count - 1], "from work"),
          "the default account's chat with a contact holds its own messages");
    cJSON_Delete(r1);
    say(conn, "{\"id\":\"r2\",\"op\":\"read_messages\",\"args\":{\"chat\":\"" MOM "\",\"account\":\"work\"}}");
    cJSON *r2 = reply("r2");
    CHECK(ok(r2) && strstr(outbox[outbox_count - 1], "from work") && !strstr(outbox[outbox_count - 1], "from main"),
          "and the other account's chat with them holds the other's");
    cJSON_Delete(r2);
    clear_outbox();
}

static void test_each_access_level(void) {
    int conn = open_client();
    clear_outbox();
    World *main_world = &worlds[0], *work_world = &worlds[1];

    /* work is at read: nothing may be sent from it. */
    say(conn, "{\"id\":\"s1\",\"op\":\"send_message\",\"args\":{\"chat\":\"" MOM "\",\"text\":\"hi from work\",\"account\":\"work\"}}");
    cJSON *s1 = reply("s1");
    CHECK(s1 && !ok(s1) && !strcmp(error_code(s1), "not_allowed") && approval_queue_count(queue) == 0 && work_world->texts == 0,
          "an account at read refuses a send, and you are not asked");
    cJSON_Delete(s1);

    /* main follows the setting, which is send: the send waits for you and then goes out through main. */
    say(conn, "{\"id\":\"s2\",\"op\":\"send_message\",\"args\":{\"chat\":\"" MOM "\",\"text\":\"hi from main\"}}");
    CHECK(approval_queue_count(queue) == 1, "an account at send asks you first");
    const ApprovalRequest *asked = approval_queue_at(queue, 0);
    CHECK(asked && strstr(asked->chat_name, "[main]"), "what you are asked names the account");
    answer_first(1);
    cJSON *s2 = reply("s2");
    CHECK(ok(s2) && main_world->texts == 1 && work_world->texts == 0 && !strcmp(main_world->last_text, "hi from main"),
          "once approved it goes out through that account and no other");
    cJSON_Delete(s2);

    /* Raising work to send lets it send, through its own gateway. Another request is served in between. */
    account_roster_manager_set_agent_access(roster, work_world->id, ACCOUNT_AGENT_SEND);
    say(conn, "{\"id\":\"s3\",\"op\":\"send_message\",\"args\":{\"chat\":\"" MOM "\",\"text\":\"hi from work\",\"account\":\"work\"}}");
    CHECK(approval_queue_count(queue) == 1, "with its level raised the same send waits for you");
    say(conn, "{\"id\":\"between\",\"op\":\"list_chats\"}");
    answer_first(1);
    cJSON *s3 = reply("s3");
    CHECK(ok(s3) && work_world->texts == 1 && main_world->texts == 1 && !strcmp(work_world->last_text, "hi from work"),
          "a send that waited goes out through the account it was asked of, whoever was served meanwhile");
    cJSON_Delete(s3);

    /* Closing an account while a send waits. */
    say(conn, "{\"id\":\"s4\",\"op\":\"send_message\",\"args\":{\"chat\":\"" MOM "\",\"text\":\"too late\",\"account\":\"work\"}}");
    account_roster_manager_set_agent_access(roster, work_world->id, ACCOUNT_AGENT_OFF);
    answer_first(1);
    cJSON *s4 = reply("s4");
    CHECK(s4 && !ok(s4) && !strcmp(error_code(s4), "not_allowed") && work_world->texts == 1,
          "a send waiting on an account that is then closed to agents does not go out");
    cJSON_Delete(s4);
    account_roster_manager_set_agent_access(roster, work_world->id, ACCOUNT_AGENT_READ);
    clear_outbox();
}

static void test_events_say_whose(AccountId work, AccountId closed) {
    int conn = open_client();
    say(conn, "{\"id\":\"sub\",\"op\":\"subscribe\",\"args\":{\"chats\":\"all\"}}");
    clear_outbox();

    incoming(&worlds[1], "W9", BOSS, "meeting at 3");
    tick();
    cJSON *e = event("message");
    CHECK(e && account_id_of(e) == work, "a message that reaches an account is pushed marked with that account");
    cJSON_Delete(e);
    clear_outbox();

    incoming(&worlds[0], "M9", MOM, "supper?");
    tick();
    e = event("message");
    CHECK(e && account_id_of(e) == ACCOUNT_ID_FIRST, "and one for another account with that one");
    cJSON_Delete(e);
    clear_outbox();

    incoming(&worlds[2], "C9", MOM, "for no agent's eyes");
    tick();
    tick();
    e = event("message");
    CHECK(e == NULL, "a message to an account closed to agents is not pushed at all");
    cJSON_Delete(e);
    (void)closed;
    clear_outbox();
}

static void test_no_account_open(void) {
    Account all[ACCOUNT_MAX];
    int n = account_roster_manager_list(roster, all, ACCOUNT_MAX);
    for (int i = 0; i < n; i++) account_roster_manager_set_agent_access(roster, all[i].id, ACCOUNT_AGENT_OFF);
    int conn = open_client();
    cJSON *h = reply("h");
    const cJSON *r = result(h);
    const cJSON *jid = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "account"), "jid");
    CHECK(ok(h) && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(r, "accounts")) == 0, "with every account closed an agent is greeted with an empty list");
    CHECK(cJSON_IsString(jid) && jid->valuestring[0] == '\0', "and is told nobody's number");
    cJSON_Delete(h);
    clear_outbox();
    say(conn, "{\"id\":\"x\",\"op\":\"list_chats\"}");
    cJSON *x = reply("x");
    CHECK(x && !ok(x) && !strcmp(error_code(x), "not_allowed"), "and nothing can be read");
    cJSON_Delete(x);
    clear_outbox();
}

int main(void) {
    char dir[] = "/tmp/tawk-accounts-ctl-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    char db_path[600], config[600];
    snprintf(db_path, sizeof(db_path), "%s/tawk.db", dir);
    snprintf(config, sizeof(config), "%s/config.ini", dir);
    sqlite3 *db = sqlite_database_open(db_path, NULL);
    if (!db) return 1;

    ISettingsStore *store = ini_settings_store_create(config);
    IThemeRepository *themes = json_theme_repository_create("themes", dir);
    settings_mgr = settings_manager_create(store, themes);
    settings_manager_load(settings_mgr);
    Settings s = *settings_manager_current(settings_mgr);
    s.control_socket = 1;
    s.automation_rate = 60;
    str_copy(s.automation_access, sizeof(s.automation_access), "send");      /* what the first account follows */
    settings_manager_apply(settings_mgr, &s);

    IAccountStore *account_store = sqlite_account_store_create(db);
    IChatPrefsStore *prefs = sqlite_chat_prefs_store_create(db);
    AccountRosterManagerDeps roster_deps = { account_store, prefs };
    roster = account_roster_manager_create(&roster_deps);
    AccountId work = ACCOUNT_ID_NONE, closed = ACCOUNT_ID_NONE;
    account_roster_manager_add(roster, "work", 1, &work);
    account_roster_manager_add(roster, "private", 1, &closed);
    account_roster_manager_set_agent_access(roster, work, ACCOUNT_AGENT_READ);      /* private stays off */

    IChatExporter *exporter = text_chat_exporter_create();
    make_world(&worlds[0], db, ACCOUNT_ID_FIRST, exporter, "27830000000@s.whatsapp.net");
    make_world(&worlds[1], db, work, exporter, "27830000001@s.whatsapp.net");
    make_world(&worlds[2], db, closed, exporter, "27830000002@s.whatsapp.net");
    add_chat(&worlds[0], MOM, "Mom");
    add_chat(&worlds[1], MOM, "Mom");
    add_chat(&worlds[1], BOSS, "Boss");
    add_chat(&worlds[2], MOM, "Mom");
    incoming(&worlds[0], "M1", MOM, "from main");
    incoming(&worlds[1], "W1", MOM, "from work");

    IAutomationLog *log = sqlite_automation_log_create(db);
    AutomationManagerDeps automation_deps = { log, settings_manager_current(settings_mgr), &admin_tokens };
    automation = automation_manager_create(&automation_deps);
    queue = approval_queue_create();
    ControlServerDeps control_deps = { &transport, approval_queue_prompt(queue), worlds[0].messaging, NULL, worlds[0].scheduling, NULL,
                                       automation, settings_mgr, NULL, NULL, NULL, "test", "/tmp/unused.sock", &directory, roster };
    server = control_server_create(&control_deps);
    tick();
    tick();
    clear_outbox();

    test_who_is_seen(work, closed);
    test_served_by_the_named_account(work);
    test_each_access_level();
    test_events_say_whose(work, closed);
    test_no_account_open();

    control_server_destroy(server);
    approval_queue_destroy(queue);
    automation_manager_destroy(automation);
    for (int i = 0; i < WORLD; i++) {
        World *w = &worlds[i];
        messaging_manager_destroy(w->messaging);
        scheduling_manager_destroy(w->scheduling);
        event_queue_destroy(w->events);
        w->scheduled->destroy(w->scheduled);
        w->receipts->destroy(w->receipts);
        w->reactions->destroy(w->reactions);
        w->aliases->destroy(w->aliases);
        w->contacts->destroy(w->contacts);
        w->chats->destroy(w->chats);
        w->messages->destroy(w->messages);
    }
    clear_outbox();
    log->destroy(log);
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
    if (failures == 0) printf("ok: agents see only the accounts open to them, each request is served by the account it names under that account's own access, and events say whose they are\n");
    return failures != 0;
}
