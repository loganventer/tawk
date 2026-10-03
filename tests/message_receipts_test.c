/* Message info: receipt events from the backends, the receipt store and the panel. */
#include "clients/tui/message_info_panel.h"
#include "core/event.h"
#include "resource_access/json_protocol.h"
#include "resource_access/sqlite_database.h"
#include "resource_access/sqlite_receipt_store.h"
#include "utilities/log.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static void test_protocol(void) {
    Event e;
    event_init(&e, EVENT_NONE);
    CHECK(json_protocol_decode("{\"evt\":\"receipt\",\"id\":\"ABC\",\"by\":\"27821234567@s.whatsapp.net\",\"kind\":\"read\",\"at\":1790000000}", &e) == 0 &&
          e.type == EVENT_MESSAGE_RECEIPT && strcmp(e.id, "ABC") == 0 && strcmp(e.jid, "27821234567@s.whatsapp.net") == 0 &&
          e.receipt == RECEIPT_READ && e.at == 1790000000, "a receipt event is decoded");
    event_dispose(&e);
    event_init(&e, EVENT_NONE);
    CHECK(json_protocol_decode("{\"evt\":\"receipt\",\"id\":\"ABC\",\"by\":\"x@s.whatsapp.net\",\"kind\":\"seen\",\"at\":1}", &e) != 0,
          "an unknown kind is refused");
    event_dispose(&e);
}

static void test_store(void) {
    char path[] = "/tmp/tawk-receipts-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); failures++; return; }
    close(fd);
    unlink(path);
    /* tawk logs to a file only; show it when the database will not open. */
    char log_path[] = "/tmp/tawk-receipts-log-XXXXXX";
    int log_fd = mkstemp(log_path);
    if (log_fd >= 0) close(log_fd);
    log_open(log_path, LOG_LEVEL_DEBUG);
    sqlite3 *db = sqlite_database_open(path, NULL);
    log_close();
    CHECK(db != NULL, "the database opens and migrates");
    if (!db) {
        fprintf(stderr, "sqlite %s; tawk's log:\n", sqlite3_libversion());
        FILE *f = fopen(log_path, "r");
        char line[512];
        while (f && fgets(line, sizeof(line), f)) fputs(line, stderr);
        if (f) fclose(f);
    }
    unlink(log_path);
    if (!db) return;
    IReceiptStore *s = sqlite_receipt_store_create(db, ACCOUNT_ID_FIRST);
    Receipt r[8];

    s->put(s, "M1", "ann", RECEIPT_DELIVERED, 100);
    s->put(s, "M1", "bob", RECEIPT_DELIVERED, 110);
    s->put(s, "M1", "bob", RECEIPT_READ, 150);
    s->put(s, "M1", "ann", RECEIPT_DELIVERED, 90);      /* an earlier duplicate wins */
    s->put(s, "M1", "cat", RECEIPT_PLAYED, 200);        /* played implies read and delivered */
    s->put(s, "M1", "bob", RECEIPT_READ, 300);          /* a later read keeps the first */
    s->put(s, "M2", "ann", RECEIPT_READ, 500);
    int n = s->list(s, "M1", r, 8);
    CHECK(n == 3, "three people have receipts for M1");
    CHECK(n == 3 && strcmp(r[0].jid, "bob") == 0 && r[0].read_at == 150 && r[0].delivered_at == 110,
          "the first reader comes first, with the first read time");
    CHECK(n == 3 && strcmp(r[1].jid, "cat") == 0 && r[1].read_at == 200 && r[1].played_at == 200 && r[1].delivered_at == 200,
          "played counts as read and delivered");
    CHECK(n == 3 && strcmp(r[2].jid, "ann") == 0 && r[2].delivered_at == 90 && r[2].read_at == 0,
          "delivered only comes last, with the earliest time");
    CHECK(s->put(s, "M1", "dan", RECEIPT_NONE, 1) != 0 && s->put(s, "M1", "dan", RECEIPT_READ, 0) != 0,
          "empty receipts are refused");

    s->reassign_jid(s, "ann", "ann-phone");
    n = s->list(s, "M2", r, 8);
    CHECK(n == 1 && strcmp(r[0].jid, "ann-phone") == 0, "receipts follow an alias merge");

    s->destroy(s);
    sqlite_database_close(db);
    unlink(path);
}

static void test_panel(void) {
    Message m;
    memset(&m, 0, sizeof(m));
    snprintf(m.id, sizeof(m.id), "M1");
    snprintf(m.chat_jid, sizeof(m.chat_jid), "123@g.us");
    char text[] = "Hello group\nsecond line";
    m.text = text;
    m.timestamp = 100;
    MessageInfoPanel p;
    message_info_panel_open(&p, &m, 1);
    CHECK(p.open && p.group && strcmp(p.message_id, "M1") == 0 && strcmp(p.excerpt, "Hello group") == 0,
          "the panel opens on the message, showing its first line");
    message_info_panel_key(&p, 1, KEY_DOWN);
    CHECK(p.open && p.scroll == 1, "arrows scroll");
    CHECK(message_info_panel_key(&p, 0, 27) == POPUP_CLOSED && !p.open, "Esc closes");
}

int main(void) {
    test_protocol();
    test_store();
    test_panel();
    if (failures) return 1;
    printf("ok: message info shows who received and read a message\n");
    return 0;
}
