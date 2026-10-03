/* The control client end to end, over a fake socket, with the real
 * managers on a temporary database: the handshake, what agents may see,
 * reads that leave nothing marked read, writes that wait for your answer
 * (approved, edited, declined), the rate, destructive requests that need a
 * token and then you, drafts, settings that stay out of reach, allowances
 * for the session, live messages and the automation log. */
#include "clients/control/control_server.h"
#include "clients/tui/approval_queue.h"
#include "cJSON.h"
#include "core/settings.h"
#include "engines/ai_disclaimer.h"
#include "engines/hourly_quota.h"
#include "managers/automation_manager.h"
#include "managers/messaging_manager.h"
#include "managers/scheduling_manager.h"
#include "managers/settings_manager.h"
#include "resource_access/ini_settings_store.h"
#include "resource_access/json_theme_repository.h"
#include "resource_access/sqlite_automation_log.h"
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

#define MOM    "27820000001@s.whatsapp.net"
#define MOMMY  "27820000009@s.whatsapp.net"
#define WORK   "120363000000000001@g.us"
#define SECRET "27820000002@s.whatsapp.net"
#define HIDDEN "27820000003@s.whatsapp.net"

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

/* ---- a gateway that records what was sent ---------------------------------- */

static int texts, reactions_sent, deletes;
static char last_text[512];

static int gw_ok(IMessageGateway *self) { (void)self; return 0; }
static int gw_text(IMessageGateway *self, const char *jid, const OutgoingText *t, const char *id) {
    (void)self; (void)jid; (void)id;
    texts++;
    str_copy(last_text, sizeof(last_text), t->text);
    return 0;
}
static int gw_delete(IMessageGateway *self, const DeleteRequest *r) { (void)self; (void)r; deletes++; return 0; }
static int gw_react(IMessageGateway *self, const ReactionTarget *t, const char *e) { (void)self; (void)t; (void)e; reactions_sent++; return 0; }
static int gw_jid(IMessageGateway *self, const char *jid) { (void)self; (void)jid; return 0; }
static int gw_picture(IMessageGateway *self, const char *jid, int full) { (void)self; (void)jid; (void)full; return 0; }
static int gw_typing(IMessageGateway *self, const char *jid, const char *st) { (void)self; (void)jid; (void)st; return 0; }
static int gw_presence(IMessageGateway *self, int on) { (void)self; (void)on; return 0; }
static int gw_read(IMessageGateway *self, const ReadRequest *r) { (void)self; (void)r; return 0; }
static void fake_notify(INotifier *self, const Notification *n) { (void)self; (void)n; }

/* ---- the admin token, kept in memory ------------------------------------------ */

static char admin_token[128];
static int admin_token_saves;

static int at_save(IAdminTokenStore *self, const char *token) { (void)self; str_copy(admin_token, sizeof(admin_token), token); admin_token_saves++; return 0; }
static void at_remove(IAdminTokenStore *self) { (void)self; admin_token[0] = '\0'; }
static void at_destroy(IAdminTokenStore *self) { (void)self; }
static IAdminTokenStore admin_tokens = { NULL, at_save, at_remove, at_destroy };

/* ---- the world under test ---------------------------------------------------- */

static IControlTransport transport = { NULL, ft_listen, ft_stop, ft_poll, ft_send, ft_close, NULL };
static SettingsManager *settings_mgr;
static MessagingManager *mm;
static ControlServer *server;
static ApprovalQueue *queue;
static AutomationManager *automation;
static EventQueue *events;
static IMessageStore *messages;
static IChatStore *chats;

static void tick(void) {
    ManagerChanges ch;
    messaging_manager_tick(mm, &ch);
    control_server_tick(server);
}

static int next_conn = 1;

static int open_client(const char *origin, const char *access) {
    int conn = next_conn++;
    inbox[inbox_count++] = (ControlInbound){ CONTROL_INBOUND_OPENED, conn, NULL };
    char line[256];
    snprintf(line, sizeof(line), "{\"id\":\"h\",\"op\":\"hello\",\"args\":{\"client\":\"test\",\"protocol\":1,\"origin\":\"%s\"}}", origin);
    inbox[inbox_count++] = (ControlInbound){ CONTROL_INBOUND_LINE, conn, str_dup(line) };
    if (access) {
        Settings s = *settings_manager_current(settings_mgr);
        str_copy(s.automation_access, sizeof(s.automation_access), access);
        settings_manager_apply(settings_mgr, &s);
    }
    tick();
    clear_outbox();
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

static int saw_event(const char *name) {
    for (int i = 0; i < outbox_count; i++) {
        char want[64];
        snprintf(want, sizeof(want), "\"evt\":\"%s\"", name);
        if (strstr(outbox[i], want)) return 1;
    }
    return 0;
}

static void add_chat(const char *jid, const char *name, int locked, int unread) {
    Chat c;
    chat_init(&c, jid);
    str_copy(c.name, sizeof(c.name), name);
    c.is_locked = locked;
    c.last_ts = 1790000000;
    c.unread = unread;
    chats->upsert(chats, &c);
}

static void add_message(const char *id, const char *jid, const char *text, int from_me, int64_t ts) {
    Message m;
    message_init(&m);
    str_copy(m.id, sizeof(m.id), id);
    str_copy(m.chat_jid, sizeof(m.chat_jid), jid);
    str_copy(m.sender_jid, sizeof(m.sender_jid), from_me ? "27830000000@s.whatsapp.net" : jid);
    m.from_me = from_me;
    m.status = MESSAGE_STATUS_READ;
    m.timestamp = ts;
    message_set_text(&m, text);
    messages->save(messages, &m);
    message_dispose(&m);
}

static void answer_first(int approved, const char *text, int remember) {
    const ApprovalRequest *r = approval_queue_at(queue, 0);
    if (r) approval_queue_answer(queue, r->id, approved, text, remember);
    tick();
}

/* ---- the checks ------------------------------------------------------------- */

static void test_handshake_and_reads(void) {
    int conn = next_conn++;
    inbox[inbox_count++] = (ControlInbound){ CONTROL_INBOUND_OPENED, conn, NULL };
    say(conn, "{\"id\":\"1\",\"op\":\"list_chats\"}");
    cJSON *r = reply("1");
    CHECK(r && !strcmp(error_code(r), "hello_first"), "nothing is answered before hello");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"2\",\"op\":\"hello\",\"args\":{\"protocol\":2}}");
    r = reply("2");
    CHECK(r && !strcmp(error_code(r), "protocol"), "an unknown protocol version is refused");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"3\",\"op\":\"hello\",\"args\":{\"client\":\"t\",\"protocol\":1,\"origin\":\"mcp\"}}");
    r = reply("3");
    CHECK(r && !strcmp(cJSON_GetObjectItemCaseSensitive(result(r), "access")->valuestring, "read"), "hello says what access there is");
    cJSON_Delete(r);
    say(conn, "not json");
    CHECK(outbox_count > 0 && strstr(outbox[outbox_count - 1], "bad_request"), "a line that is not JSON is a bad request");

    clear_outbox();
    say(conn, "{\"id\":\"4\",\"op\":\"list_chats\"}");
    r = reply("4");
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(result(r), "chats");
    char *text = cJSON_PrintUnformatted(list);
    CHECK(text && strstr(text, "Mom") && strstr(text, "Work"), "visible chats are listed");
    CHECK(text && !strstr(text, SECRET) && !strstr(text, HIDDEN), "locked and soft-locked chats never are");
    free(text);
    cJSON_Delete(r);

    say(conn, "{\"id\":\"5\",\"op\":\"read_messages\",\"args\":{\"chat\":\"Mom\",\"limit\":2}}");
    r = reply("5");
    const cJSON *msgs = cJSON_GetObjectItemCaseSensitive(result(r), "messages");
    CHECK(cJSON_GetArraySize(msgs) == 2, "the newest messages come back");
    CHECK(cJSON_GetObjectItemCaseSensitive(result(r), "next_before")->valuedouble == 1790000100, "with where to page back from");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"6\",\"op\":\"read_messages\",\"args\":{\"chat\":\"Mom\",\"before\":1790000100}}");
    r = reply("6");
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(result(r), "messages")) == 1, "older pages come back too");
    cJSON_Delete(r);
    Chat mom;
    CHECK(chats->get(chats, MOM, &mom) == 0 && mom.unread == 2, "reading marks nothing read");

    say(conn, "{\"id\":\"7\",\"op\":\"read_messages\",\"args\":{\"chat\":\"Mo\"}}");
    r = reply("7");
    CHECK(r && !strcmp(error_code(r), "ambiguous") &&
          cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "error"), "candidates")) == 2,
          "a name that fits two chats is ambiguous, with both");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"8\",\"op\":\"read_messages\",\"args\":{\"chat\":\"Secret\"}}");
    r = reply("8");
    CHECK(r && !strcmp(error_code(r), "not_found"), "a locked chat cannot be read by name");
    cJSON_Delete(r);

    Settings s = *settings_manager_current(settings_mgr);
    str_copy(s.automation_chats, sizeof(s.automation_chats), "Work");
    settings_manager_apply(settings_mgr, &s);
    say(conn, "{\"id\":\"9\",\"op\":\"list_chats\"}");
    r = reply("9");
    CHECK(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(result(r), "chats")) == 1, "the chat list narrows what agents see");
    cJSON_Delete(r);
    str_copy(s.automation_chats, sizeof(s.automation_chats), "");
    settings_manager_apply(settings_mgr, &s);

    say(conn, "{\"id\":\"10\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"hi\"}}");
    r = reply("10");
    CHECK(r && !strcmp(error_code(r), "not_allowed") && texts == 0, "sending is refused with access read");
    cJSON_Delete(r);
    clear_outbox();
}

static void test_writes_wait_for_you(void) {
    int conn = open_client("mcp", "send");
    say(conn, "{\"id\":\"s1\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"On my way\"}}");
    CHECK(saw_event("approval") && !reply("s1") && texts == 0, "a send waits for you, and the client is told");
    const ApprovalRequest *asked = approval_queue_at(queue, 0);
    CHECK(asked && asked->risk == APPROVAL_RISK_MEDIUM && asked->editable && !strcmp(asked->chat_name, "Mom"), "you see what, where and how risky");
    answer_first(1, NULL, 0);
    cJSON *r = reply("s1");
    CHECK(r && cJSON_GetObjectItemCaseSensitive(result(r), "id") && texts == 1 && !strcmp(last_text, "On my way"), "approved, it goes, with its id");
    cJSON_Delete(r);

    say(conn, "{\"id\":\"s2\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"On my wya\"}}");
    answer_first(1, "On my way, 10 minutes", 0);
    r = reply("s2");
    CHECK(r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result(r), "edited")) && !strcmp(last_text, "On my way, 10 minutes"),
          "your edit is what goes, and the client hears it");
    cJSON_Delete(r);

    say(conn, "{\"id\":\"s3\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"no\"}}");
    answer_first(0, NULL, 0);
    r = reply("s3");
    CHECK(r && !strcmp(error_code(r), "declined") && texts == 2, "declined, nothing goes");
    cJSON_Delete(r);

    say(conn, "{\"id\":\"s4\",\"op\":\"react\",\"args\":{\"message_id\":\"M1\",\"emoji\":\"\xF0\x9F\x91\x8D\"}}");
    CHECK(approval_queue_at(queue, 0) && approval_queue_at(queue, 0)->risk == APPROVAL_RISK_LOW, "a reaction is LOW risk");
    answer_first(1, NULL, 1);
    say(conn, "{\"id\":\"s5\",\"op\":\"react\",\"args\":{\"message_id\":\"M2\",\"emoji\":\"\xE2\x9D\xA4\"}}");
    r = reply("s5");
    CHECK(r && result(r) && approval_queue_count(queue) == 0 && reactions_sent == 2, "allowed for the session, the same again goes without asking");
    cJSON_Delete(r);

    int shell = open_client("cli", NULL);
    say(shell, "{\"id\":\"c1\",\"op\":\"send_message\",\"args\":{\"chat\":\"Work\",\"text\":\"from a script\"}}");
    r = reply("c1");
    CHECK(r && result(r) && texts == 3 && approval_queue_count(queue) == 0, "your own shell command sends without asking");
    cJSON_Delete(r);


    say(conn, "{\"id\":\"d1\",\"op\":\"draft_message\",\"args\":{\"chat\":\"Work\",\"text\":\"a draft\"}}");
    r = reply("d1");
    CHECK(r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result(r), "drafted")) && approval_queue_count(queue) == 0, "a draft needs nobody's answer");
    cJSON_Delete(r);
    char jid[128];
    char *offered = messaging_manager_take_offered_draft(mm, jid, sizeof(jid));
    CHECK(offered && !strcmp(offered, "a draft") && !strcmp(jid, WORK), "and is handed to the screen");
    free(offered);
    say(conn, "{\"id\":\"d2\",\"op\":\"draft_message\",\"args\":{\"chat\":\"Work\",\"text\":\"another\"}}");
    r = reply("d2");
    CHECK(r && !strcmp(error_code(r), "draft_exists"), "a waiting draft is never overwritten");
    cJSON_Delete(r);
    clear_outbox();
}

static void test_destructive_and_manage(void) {
    int conn = open_client("mcp", "send");
    say(conn, "{\"id\":\"m1\",\"op\":\"delete_message\",\"args\":{\"message_id\":\"M1\"}}");
    cJSON *r = reply("m1");
    CHECK(r && !strcmp(error_code(r), "not_allowed"), "deleting needs access manage");
    cJSON_Delete(r);

    Settings s = *settings_manager_current(settings_mgr);
    str_copy(s.automation_access, sizeof(s.automation_access), "manage");
    settings_manager_apply(settings_mgr, &s);
    say(conn, "{\"id\":\"m2\",\"op\":\"delete_message\",\"args\":{\"message_id\":\"M1\"}}");
    r = reply("m2");
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(result(r), "token");
    CHECK(r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result(r), "needs_confirmation")) && cJSON_IsString(token) &&
          approval_queue_count(queue) == 0 && deletes == 0, "the first call only hands back a token");
    char tok[64] = "";
    if (cJSON_IsString(token)) str_copy(tok, sizeof(tok), token->valuestring);
    cJSON_Delete(r);

    int other = open_client("mcp", NULL);
    char line[256];
    snprintf(line, sizeof(line), "{\"id\":\"m3\",\"op\":\"confirm\",\"args\":{\"token\":\"%s\"}}", tok);
    say(other, line);
    r = reply("m3");
    CHECK(r && !strcmp(error_code(r), "bad_token"), "a token only works on its own connection");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"m4\",\"op\":\"confirm\",\"args\":{\"token\":\"0000\"}}");
    r = reply("m4");
    CHECK(r && !strcmp(error_code(r), "bad_token"), "a made-up token does nothing");
    cJSON_Delete(r);

    snprintf(line, sizeof(line), "{\"id\":\"m5\",\"op\":\"confirm\",\"args\":{\"token\":\"%s\"}}", tok);
    say(conn, line);
    const ApprovalRequest *asked = approval_queue_at(queue, 0);
    CHECK(asked && asked->risk == APPROVAL_RISK_HIGH && asked->danger && deletes == 0, "confirmed, it still waits for you, as HIGH");
    answer_first(1, NULL, 1);
    r = reply("m5");
    Message gone;
    CHECK(r && result(r) && messages->get(messages, "M1", &gone) != 0, "then it is done");
    cJSON_Delete(r);
    say(conn, line);
    r = reply("m5");
    CHECK(r && !strcmp(error_code(r), "bad_token"), "a token works once");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"m6\",\"op\":\"delete_message\",\"args\":{\"message_id\":\"M2\"}}");
    r = reply("m6");
    CHECK(r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result(r), "needs_confirmation")),
          "a destructive request is never allowed for the session");
    cJSON_Delete(r);

    int shell = open_client("cli", NULL);
    say(shell, "{\"id\":\"m7\",\"op\":\"clear_chat\",\"args\":{\"chat\":\"Work\"}}");
    r = reply("m7");
    CHECK(r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result(r), "needs_confirmation")), "even your shell needs two steps to destroy");
    cJSON_Delete(r);

    say(conn, "{\"id\":\"x1\",\"op\":\"set_setting\",\"args\":{\"section\":\"automation\",\"key\":\"access\",\"value\":\"manage\"}}");
    r = reply("x1");
    CHECK(r && !strcmp(error_code(r), "not_allowed"), "agents cannot change their own permissions");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"x2\",\"op\":\"set_setting\",\"args\":{\"section\":\"screensaver\",\"key\":\"command\",\"value\":\"rm -rf ~\"}}");
    r = reply("x2");
    CHECK(r && !strcmp(error_code(r), "not_allowed"), "nor any setting that runs a program");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"x3\",\"op\":\"set_setting\",\"args\":{\"section\":\"appearance\",\"key\":\"use_24h_clock\",\"value\":\"off\"}}");
    answer_first(1, NULL, 0);
    r = reply("x3");
    CHECK(r && !strcmp(cJSON_GetObjectItemCaseSensitive(result(r), "value")->valuestring, "false") &&
          !settings_manager_current(settings_mgr)->use_24h_clock, "an ordinary setting changes once you allow it");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"x4\",\"op\":\"get_settings\"}");
    r = reply("x4");
    char *all = cJSON_PrintUnformatted(result(r));
    CHECK(all && strstr(all, "\"key\":\"access\"") && strstr(all, "\"changeable\":false"), "settings list what may be changed");
    free(all);
    cJSON_Delete(r);
    clear_outbox();
}

/* Last, since it runs the bucket dry. */
/* With the disclaimer on, what an agent sends says an AI wrote it; your own shell commands are left alone. */
static void test_disclaimer(void) {
    int conn = open_client("mcp", "send");
    int shell = open_client("cli", NULL);
    Settings s = *settings_manager_current(settings_mgr);
    s.automation_disclaimer = 1;
    s.automation_rate = 60;
    str_copy(s.automation_disclaimer_text, sizeof(s.automation_disclaimer_text), "Created with my AI assistant");
    settings_manager_apply(settings_mgr, &s);
    say(conn, "{\"id\":\"x1\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"See you at six\"}}");
    const ApprovalRequest *asked = approval_queue_at(queue, 0);
    CHECK(asked && asked->text && !strcmp(asked->text, "See you at six"), "you are asked about the words alone");
    answer_first(1, "See you at seven", 0);
    cJSON *r = reply("x1");
    CHECK(!strcmp(last_text, "See you at seven\n\nCreated with my AI assistant"), "the disclaimer goes under what you approved, after your edit");
    CHECK(r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result(r), "disclaimer")), "and the agent is told it was added");
    cJSON_Delete(r);
    say(shell, "{\"id\":\"x2\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"from me\"}}");
    CHECK(!strcmp(last_text, "from me"), "your own shell command gets none");
    s.automation_disclaimer = 0;
    settings_manager_apply(settings_mgr, &s);
    say(conn, "{\"id\":\"x3\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"plain\"}}");
    answer_first(1, NULL, 0);
    CHECK(!strcmp(last_text, "plain"), "and off, nothing is added");
    char *twice = ai_disclaimer_append("hello\n\nCreated with my AI assistant", "Created with my AI assistant");
    CHECK(!twice, "it is never added twice");
    free(twice);
    clear_outbox();
}

static void approve(int conn, const char *id, const char *request, const char *token) {
    char line[400];
    snprintf(line, sizeof(line), "{\"id\":\"%s\",\"op\":\"approve\",\"args\":{\"id\":\"%s\",\"admin_token\":\"%s\"}}", id, request, token);
    say(conn, line);
}

static void set_automation(const char *access, const char *chat_list, int per_hour) {
    Settings s = *settings_manager_current(settings_mgr);
    str_copy(s.automation_access, sizeof(s.automation_access), access);
    str_copy(s.automation_self_chats, sizeof(s.automation_self_chats), chat_list);
    s.automation_self_per_hour = per_hour;
    s.automation_rate = 60;
    settings_manager_apply(settings_mgr, &s);
    tick();
}

/* Access admin: a client holding the admin token answers its own sends, in the chats you named, so many an hour. */
static void test_admin_answers_its_own(void) {
    int conn = open_client("mcp", "manage");
    set_automation("manage", "Mom", 2);
    int sent = texts;
    CHECK(!admin_token[0], "there is no admin token below access admin");
    say(conn, "{\"id\":\"a1\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"one\"}}");
    approve(conn, "p1", "a1", "anything");
    cJSON *r = reply("p1");
    CHECK(r && !strcmp(error_code(r), "not_allowed") && texts == sent && approval_queue_count(queue) == 1, "below admin nothing answers itself, and the request still waits for you");
    cJSON_Delete(r);

    set_automation("admin", "Mom", 2);
    CHECK(strlen(admin_token) >= 64, "access admin writes an admin token");
    char token[128];
    str_copy(token, sizeof(token), admin_token);
    approve(conn, "p2", "a1", "wrong");
    r = reply("p2");
    CHECK(r && !strcmp(error_code(r), "bad_token") && texts == sent, "the wrong token answers nothing");
    cJSON_Delete(r);
    int other = open_client("mcp", NULL);
    approve(other, "p3", "a1", token);
    r = reply("p3");
    CHECK(r && !strcmp(error_code(r), "not_found") && texts == sent, "another client cannot answer a request that is not its own");
    cJSON_Delete(r);
    approve(conn, "p4", "a1", token);
    r = reply("p4");
    cJSON *done = reply("a1");
    CHECK(r && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result(r), "approved")) && done && result(done) && texts == sent + 1,
          "with the token its own send goes, and the waiting request gets its answer");
    CHECK(approval_queue_count(queue) == 0, "and you are no longer asked");
    cJSON_Delete(r);
    cJSON_Delete(done);
    char notice[256];
    CHECK(automation_manager_take_notice(automation, notice, sizeof(notice)) && strstr(notice, "Mom"), "you are told what it did");
    AutomationEntry *log = NULL;
    int n = 0;
    automation_manager_recent(automation, 1, &log, &n);
    CHECK(n == 1 && log[0].outcome == AUTOMATION_OUTCOME_SELF_APPROVED, "and the log says the agent approved it");
    free(log);

    say(conn, "{\"id\":\"a2\",\"op\":\"send_message\",\"args\":{\"chat\":\"Work\",\"text\":\"two\"}}");
    approve(conn, "p5", "a2", token);
    r = reply("p5");
    CHECK(r && !strcmp(error_code(r), "not_allowed") && texts == sent + 1 && approval_queue_count(queue) == 1, "a chat you did not choose for this waits for you");
    cJSON_Delete(r);
    answer_first(0, NULL, 0);
    say(conn, "{\"id\":\"a3\",\"op\":\"set_chat\",\"args\":{\"chat\":\"Mom\",\"pinned\":true}}");
    approve(conn, "p6", "a3", token);
    r = reply("p6");
    CHECK(r && !strcmp(error_code(r), "not_allowed") && approval_queue_count(queue) == 1, "changes that are not sends wait for you");
    cJSON_Delete(r);
    answer_first(0, NULL, 0);

    set_automation("admin", "", 2);
    say(conn, "{\"id\":\"a4\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"three\"}}");
    approve(conn, "p7", "a4", token);
    r = reply("p7");
    CHECK(r && !strcmp(error_code(r), "not_allowed") && texts == sent + 1, "with no chats chosen, none is answered this way");
    cJSON_Delete(r);
    answer_first(0, NULL, 0);

    set_automation("admin", "*", 2);
    say(conn, "{\"id\":\"a5\",\"op\":\"react\",\"args\":{\"message_id\":\"M2\",\"emoji\":\"\xF0\x9F\x91\x8D\"}}");
    approve(conn, "p8", "a5", token);
    r = reply("p8");
    CHECK(r && result(r), "with every chat chosen a reaction is answered too, the second this hour");
    cJSON_Delete(r);
    say(conn, "{\"id\":\"a6\",\"op\":\"send_message\",\"args\":{\"chat\":\"Mom\",\"text\":\"four\"}}");
    approve(conn, "p9", "a6", token);
    r = reply("p9");
    CHECK(r && !strcmp(error_code(r), "rate_limited") && approval_queue_count(queue) == 1, "past the hour's allowance it waits for you again");
    cJSON_Delete(r);
    answer_first(0, NULL, 0);

    set_automation("send", "", 2);
    CHECK(!admin_token[0], "leaving admin removes the token");
    set_automation("admin", "Mom", 2);
    CHECK(admin_token[0] && strcmp(admin_token, token) != 0, "and coming back makes a new one");
    set_automation("send", "", 20);
    while (automation_manager_take_notice(automation, notice, sizeof(notice))) {}

    HourlyQuota q;
    hourly_quota_init(&q);
    int retry = 0;
    CHECK(hourly_quota_take(&q, 2, 1000, &retry) && hourly_quota_take(&q, 2, 2000, &retry) && !hourly_quota_take(&q, 2, 3000, &retry) && retry == 3598,
          "the hourly allowance says when the next one is free");
    CHECK(hourly_quota_take(&q, 2, 1000 + 3600000, &retry), "and frees one as the oldest lapses");
    clear_outbox();
}

static void test_rate(void) {
    int shell = open_client("cli", "send");
    cJSON *r;
    Settings s = *settings_manager_current(settings_mgr);
    s.automation_rate = 1;
    settings_manager_apply(settings_mgr, &s);
    say(shell, "{\"id\":\"c2\",\"op\":\"send_message\",\"args\":{\"chat\":\"Work\",\"text\":\"one\"}}");
    say(shell, "{\"id\":\"c3\",\"op\":\"send_message\",\"args\":{\"chat\":\"Work\",\"text\":\"two\"}}");
    r = reply("c3");
    CHECK(r && !strcmp(error_code(r), "rate_limited") && cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "error"), "retry_after"),
          "past the rate, writes are refused with when to try again");
    cJSON_Delete(r);
    s.automation_rate = 60;
    settings_manager_apply(settings_mgr, &s);
    clear_outbox();
}

static void test_live_and_log(void) {
    int conn = open_client("mcp", NULL);
    say(conn, "{\"id\":\"l1\",\"op\":\"subscribe\",\"args\":{\"chats\":\"all\"}}");
    cJSON *r = reply("l1");
    CHECK(r && result(r), "subscribing works");
    cJSON_Delete(r);
    clear_outbox();
    Event e;
    event_init(&e, EVENT_MESSAGE_UPSERT);
    e.live = 1;
    str_copy(e.message.id, sizeof(e.message.id), "LIVE1");
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), MOM);
    str_copy(e.message.sender_jid, sizeof(e.message.sender_jid), MOM);
    e.message.timestamp = 1790000200;
    message_set_text(&e.message, "Are you coming?");
    event_queue_push(events, &e);
    event_init(&e, EVENT_MESSAGE_UPSERT);
    e.live = 1;
    str_copy(e.message.id, sizeof(e.message.id), "LIVE2");
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), SECRET);
    str_copy(e.message.sender_jid, sizeof(e.message.sender_jid), SECRET);
    e.message.timestamp = 1790000201;
    message_set_text(&e.message, "psst");
    event_queue_push(events, &e);
    tick();
    tick();
    int mom = 0, secret = 0;
    for (int i = 0; i < outbox_count; i++) {
        mom |= strstr(outbox[i], "Are you coming?") != NULL;
        secret |= strstr(outbox[i], "psst") != NULL;
    }
    CHECK(mom && saw_event("message"), "a new message reaches subscribers");
    CHECK(!secret, "one in a locked chat does not");

    /* The two push settings decide what an agent hears as it happens; your own shell always hears. */
    int shell = open_client("cli", NULL);
    say(shell, "{\"id\":\"l2\",\"op\":\"subscribe\",\"args\":{\"chats\":\"all\"}}");
    Settings push = *settings_manager_current(settings_mgr);
    push.automation_push_received = 0;
    push.automation_push_sent = 0;
    settings_manager_apply(settings_mgr, &push);
    clear_outbox();
    event_init(&e, EVENT_MESSAGE_UPSERT);
    e.live = 1;
    str_copy(e.message.id, sizeof(e.message.id), "LIVE3");
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), MOM);
    str_copy(e.message.sender_jid, sizeof(e.message.sender_jid), MOM);
    e.message.timestamp = 1790000202;
    message_set_text(&e.message, "Hello?");
    event_queue_push(events, &e);
    tick();
    tick();
    int heard = 0;
    for (int i = 0; i < outbox_count; i++) heard += strstr(outbox[i], "Hello?") != NULL;
    CHECK(heard == 1, "with pushing off the agent hears nothing new, and your shell still does");
    push.automation_push_sent = 1;
    settings_manager_apply(settings_mgr, &push);
    clear_outbox();
    event_init(&e, EVENT_MESSAGE_UPSERT);
    e.live = 1;
    e.message.from_me = 1;
    str_copy(e.message.id, sizeof(e.message.id), "LIVE4");
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), MOM);
    e.message.timestamp = 1790000203;
    message_set_text(&e.message, "Coming now");
    event_queue_push(events, &e);
    tick();
    tick();
    heard = 0;
    for (int i = 0; i < outbox_count; i++) heard += strstr(outbox[i], "Coming now") != NULL;
    CHECK(heard == 2, "what you send is pushed by its own setting");
    push.automation_push_received = 1;
    settings_manager_apply(settings_mgr, &push);

    /* Someone reading a message of yours is its own event, with its own setting, off until you turn it on. */
    clear_outbox();
    event_init(&e, EVENT_MESSAGE_RECEIPT);
    str_copy(e.id, sizeof(e.id), "LIVE4");
    str_copy(e.jid, sizeof(e.jid), MOM);
    e.receipt = RECEIPT_READ;
    e.at = 1790000300;
    event_queue_push(events, &e);
    tick();
    tick();
    CHECK(!saw_event("read"), "a read is not pushed until you turn it on");
    push.automation_push_read = 1;
    settings_manager_apply(settings_mgr, &push);
    event_init(&e, EVENT_MESSAGE_RECEIPT);
    str_copy(e.id, sizeof(e.id), "LIVE4");
    str_copy(e.jid, sizeof(e.jid), MOM);
    e.receipt = RECEIPT_READ;
    e.at = 1790000301;
    event_queue_push(events, &e);
    event_init(&e, EVENT_MESSAGE_RECEIPT);
    str_copy(e.id, sizeof(e.id), "LIVE4");
    str_copy(e.jid, sizeof(e.jid), MOM);
    e.receipt = RECEIPT_DELIVERED;
    e.at = 1790000302;
    event_queue_push(events, &e);
    tick();
    tick();
    int reads_pushed = 0;
    for (int i = 0; i < outbox_count; i++) reads_pushed += strstr(outbox[i], "\"evt\":\"read\"") != NULL && strstr(outbox[i], "LIVE4") != NULL;
    CHECK(reads_pushed == 1, "turned on, the agent hears who read which message, once, and your shell does not");
    /* Reactions to your messages and edits by others: each its own event and its own switch. */
    clear_outbox();
    push.automation_push_reactions = 1;
    push.automation_push_edits = 1;
    push.automation_push_scheduled = 1;
    settings_manager_apply(settings_mgr, &push);
    event_init(&e, EVENT_REACTION);
    e.live = 1;
    str_copy(e.id, sizeof(e.id), "LIVE4");
    str_copy(e.jid, sizeof(e.jid), MOM);
    str_copy(e.emoji, sizeof(e.emoji), "\xF0\x9F\x91\x8D");
    str_copy(e.chat.jid, sizeof(e.chat.jid), MOM);
    event_queue_push(events, &e);
    event_init(&e, EVENT_MESSAGE_EDIT);
    e.live = 1;
    str_copy(e.message.id, sizeof(e.message.id), "LIVE3");
    str_copy(e.message.chat_jid, sizeof(e.message.chat_jid), MOM);
    message_set_text(&e.message, "Hello there?");
    event_queue_push(events, &e);
    tick();
    tick();
    messaging_manager_note_scheduled_sent(mm, "SCHED1", MOM);
    tick();
    int reacted = 0, edited = 0, scheduled = 0;
    for (int i = 0; i < outbox_count; i++) {
        reacted += strstr(outbox[i], "\"evt\":\"reaction\"") != NULL && strstr(outbox[i], "LIVE4") != NULL;
        edited += strstr(outbox[i], "\"evt\":\"edit\"") != NULL && strstr(outbox[i], "Hello there?") != NULL;
        scheduled += strstr(outbox[i], "\"evt\":\"scheduled_sent\"") != NULL && strstr(outbox[i], "SCHED1") != NULL;
    }
    CHECK(reacted == 1, "a reaction to your message is pushed to the agent");
    CHECK(edited == 1, "an edit by someone else is pushed with the message as it now reads");
    CHECK(scheduled == 1, "a scheduled message going out is pushed by its id");
    push.automation_push_reactions = push.automation_push_edits = push.automation_push_scheduled = 0;
    settings_manager_apply(settings_mgr, &push);
    clear_outbox();
    event_init(&e, EVENT_REACTION);
    e.live = 1;
    str_copy(e.id, sizeof(e.id), "LIVE4");
    str_copy(e.jid, sizeof(e.jid), MOM);
    str_copy(e.chat.jid, sizeof(e.chat.jid), MOM);
    event_queue_push(events, &e);
    tick();
    tick();
    CHECK(!saw_event("reaction"), "and not with its switch off");
    push.automation_push_read = 0;
    settings_manager_apply(settings_mgr, &push);

    AutomationEntry *log = NULL;
    int n = 0;
    automation_manager_recent(automation, 200, &log, &n);
    int approved = 0, declined = 0, reads = 0, refused = 0;
    for (int i = 0; i < n; i++) {
        approved += log[i].outcome == AUTOMATION_OUTCOME_APPROVED;
        declined += log[i].outcome == AUTOMATION_OUTCOME_DECLINED;
        reads += log[i].outcome == AUTOMATION_OUTCOME_READ;
        refused += log[i].outcome == AUTOMATION_OUTCOME_REFUSED;
    }
    free(log);
    CHECK(approved >= 4 && declined >= 1 && reads >= 3 && refused >= 2, "everything is in the automation log");
    const AutomationStatus *st = automation_manager_status(automation);
    CHECK(st->listening && st->session_count >= 4, "the status says who is connected");
    clear_outbox();
}

int main(void) {
    char dir[] = "/tmp/tawk-control-XXXXXX";
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
    s.automation_rate = 60;                         /* a test writes faster than a person */
    settings_manager_apply(settings_mgr, &s);

    IMessageGateway gw;
    memset(&gw, 0, sizeof(gw));
    gw.start = gw_ok;
    gw.connect = gw_ok;
    gw.send_text = gw_text;
    gw.delete_message = gw_delete;
    gw.react = gw_react;
    gw.subscribe = gw_jid;
    gw.request_profile = gw_jid;
    gw.request_picture = gw_picture;
    gw.typing = gw_typing;
    gw.presence = gw_presence;
    gw.mark_read = gw_read;
    INotifier notifier = { NULL, fake_notify, NULL };
    events = event_queue_create(64);
    messages = sqlite_message_store_create(db, ACCOUNT_ID_FIRST);
    chats = sqlite_chat_store_create(db, ACCOUNT_ID_FIRST);
    IContactStore *contacts = sqlite_contact_store_create(db, ACCOUNT_ID_FIRST);
    IJidAliasStore *aliases = sqlite_jid_alias_store_create(db, ACCOUNT_ID_FIRST);
    IReactionStore *reaction_store = sqlite_reaction_store_create(db, ACCOUNT_ID_FIRST);
    IReceiptStore *receipts = sqlite_receipt_store_create(db, ACCOUNT_ID_FIRST);
    IChatExporter *exporter = text_chat_exporter_create();
    IScheduledMessageStore *scheduled = sqlite_scheduled_message_store_create(db, ACCOUNT_ID_FIRST);
    IAutomationLog *log = sqlite_automation_log_create(db);
    add_chat(MOM, "Mom", 0, 2);
    add_chat(MOMMY, "Mommy", 0, 0);
    add_chat(WORK, "Work", 0, 0);
    add_chat(SECRET, "Secret", 1, 0);
    add_chat(HIDDEN, "Hidden", 0, 0);
    chats->set_soft_locked(chats, HIDDEN, 1);
    add_message("M0", MOM, "Morning", 0, 1790000000);
    add_message("M1", MOM, "See you at 6", 0, 1790000100);
    add_message("M2", MOM, "Bring bread", 0, 1790000150);
    MessagingManagerDeps deps = { &gw, messages, chats, contacts, aliases, reaction_store, receipts, &notifier, events,
                                  settings_manager_current(settings_mgr), NULL, exporter, NULL, NULL, ACCOUNT_ID_FIRST, NULL };
    mm = messaging_manager_create(&deps);
    SchedulingManagerDeps sched_deps = { scheduled };
    SchedulingManager *scheduling = scheduling_manager_create(&sched_deps);
    AutomationManagerDeps automation_deps = { log, settings_manager_current(settings_mgr), &admin_tokens };
    automation = automation_manager_create(&automation_deps);
    queue = approval_queue_create();
    ControlServerDeps control_deps = { &transport, approval_queue_prompt(queue), mm, NULL, scheduling, NULL, automation,
                                       settings_mgr, NULL, NULL, NULL, "test", "/tmp/unused.sock", NULL, NULL };
    server = control_server_create(&control_deps);


    messaging_manager_start(mm);
    Event connected;
    event_init(&connected, EVENT_AUTH_CONNECTED);
    str_copy(connected.jid, sizeof(connected.jid), "27830000000@s.whatsapp.net");
    str_copy(connected.name, sizeof(connected.name), "Logan");
    event_queue_push(events, &connected);
    tick();

    test_handshake_and_reads();
    test_writes_wait_for_you();
    test_destructive_and_manage();
    test_live_and_log();
    test_disclaimer();
    test_admin_answers_its_own();
    test_rate();

    control_server_destroy(server);
    approval_queue_destroy(queue);
    automation_manager_destroy(automation);
    messaging_manager_destroy(mm);
    scheduling_manager_destroy(scheduling);
    event_queue_destroy(events);
    clear_outbox();
    log->destroy(log);
    scheduled->destroy(scheduled);
    exporter->destroy(exporter);
    receipts->destroy(receipts);
    reaction_store->destroy(reaction_store);
    aliases->destroy(aliases);
    contacts->destroy(contacts);
    chats->destroy(chats);
    messages->destroy(messages);
    sqlite_database_close(db);
    settings_manager_destroy(settings_mgr);
    themes->destroy(themes);
    store->destroy(store);
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", dir);

    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("ok: agents see only what they may, reads mark nothing, writes wait for you, destructive ones need a token and you, and it is all logged\n");
    return 0;
}
