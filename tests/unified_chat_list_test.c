/* Several accounts' chats as one list: which chats of one contact show as
 * one, what a merged row says, which account sends, and the list of one
 * account alone. */
#include "clients/tui/unified_chat_list.h"
#include "engines/chat_merge_policy.h"
#include "engines/reply_account_policy.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

#define MOM  "27820000001@s.whatsapp.net"
#define DAD  "27820000002@s.whatsapp.net"
#define FAM  "120363000000000001@g.us"
#define MAIN 1
#define WORK 2
#define SIDE 5

static Chat chat(const char *jid, const char *name, int64_t ts, int unread) {
    Chat c;
    chat_init(&c, jid);
    c.is_archived = 0;
    c.is_locked = 0;
    str_copy(c.name, sizeof(c.name), name);
    snprintf(c.preview, sizeof(c.preview), "at %lld", (long long)ts);
    c.last_ts = ts;
    c.unread = unread;
    return c;
}

/* What was chosen for each contact in a test. */
static ChatPrefs s_mom, s_fam;

static void prefs(void *ctx, const char *jid, ChatPrefs *out) {
    (void)ctx;
    if (strcmp(jid, MOM) == 0) *out = s_mom;
    else if (strcmp(jid, FAM) == 0) *out = s_fam;
}

static const Chat *row(const UnifiedChatList *list, const char *jid, AccountId account) {
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->rows[i].jid, jid) == 0 && (account == ACCOUNT_ID_NONE || list->rows[i].account == account)) return &list->rows[i];
    }
    return NULL;
}

static int rows_for(const UnifiedChatList *list, const char *jid) {
    int n = 0;
    for (int i = 0; i < list->count; i++) n += strcmp(list->rows[i].jid, jid) == 0;
    return n;
}

static void test_policies(void) {
    CHECK(chat_merge_policy_merges(1, CHAT_MERGE_FOLLOW) && !chat_merge_policy_merges(0, CHAT_MERGE_FOLLOW), "a contact with nothing chosen follows the setting");
    CHECK(chat_merge_policy_merges(0, CHAT_MERGE_ALWAYS), "always merges even with the setting off");
    CHECK(!chat_merge_policy_merges(1, CHAT_MERGE_NEVER), "never keeps them apart even with the setting on");

    ReplyAccountCandidate both[] = { { MAIN, 1, 100 }, { WORK, 1, 200 }, { SIDE, 0, 0 } };
    CHECK(reply_account_policy_choose(WORK, MAIN, MAIN, both, 3) == WORK, "the account picked for this message comes first");
    CHECK(reply_account_policy_choose(ACCOUNT_ID_NONE, WORK, MAIN, both, 3) == WORK, "then the contact's own sending account");
    CHECK(reply_account_policy_choose(ACCOUNT_ID_NONE, ACCOUNT_ID_NONE, MAIN, both, 3) == MAIN, "then the primary account");
    CHECK(reply_account_policy_choose(SIDE, ACCOUNT_ID_NONE, MAIN, both, 3) == MAIN, "an account picked for the message that has no chat with them is passed over");
    CHECK(reply_account_policy_choose(ACCOUNT_ID_NONE, SIDE, MAIN, both, 3) == MAIN, "so is a contact's sending account that no longer has the chat");
    CHECK(reply_account_policy_choose(ACCOUNT_ID_NONE, ACCOUNT_ID_NONE, SIDE, both, 3) == WORK, "with no rule left, the account they wrote to last");
    CHECK(reply_account_policy_choose(ACCOUNT_ID_NONE, 99, 98, both, 3) == WORK, "accounts that are gone are passed over too");
    ReplyAccountCandidate none[] = { { MAIN, 0, 0 }, { WORK, 0, 0 } };
    CHECK(reply_account_policy_choose(MAIN, MAIN, MAIN, none, 2) == ACCOUNT_ID_NONE, "nobody sends to a contact no account has a chat with");
}

static void test_list(void) {
    Chat main_chats[] = { chat(MOM, "Mom", 100, 2), chat(DAD, "Dad", 300, 0), chat(FAM, "Family", 50, 1) };
    Chat work_chats[] = { chat(MOM, "Mother", 400, 3), chat(FAM, "Family", 60, 4) };
    main_chats[0].is_pinned = 1;
    main_chats[2].is_muted = 1;
    work_chats[1].unread_mention = 1;
    ChatSource sources[] = { { MAIN, main_chats, 3 }, { WORK, work_chats, 2 } };
    UnifiedChatRules rules = { ACCOUNT_ID_NONE, 1, MAIN, prefs, NULL };
    UnifiedChatList list;
    unified_chat_list_init(&list);
    memset(&s_mom, 0, sizeof(s_mom));
    memset(&s_fam, 0, sizeof(s_fam));

    unified_chat_list_build(&list, sources, 2, &rules);
    CHECK(list.count == 3 && rows_for(&list, MOM) == 1 && rows_for(&list, FAM) == 1 && rows_for(&list, DAD) == 1,
          "with merging on, a contact on two accounts is one row");
    const Chat *mom = row(&list, MOM, ACCOUNT_ID_NONE);
    CHECK(mom && mom->accounts == 3u && mom->unread == 5 && mom->last_ts == 400 && strcmp(mom->preview, "at 400") == 0,
          "the row says both accounts have it, adds their unread counts and shows the newest message");
    CHECK(mom && strcmp(mom->name, "Mom") == 0 && mom->is_pinned, "it keeps a name and is pinned when either is");
    CHECK(mom && mom->account == MAIN, "it acts through the primary account");
    const Chat *fam = row(&list, FAM, ACCOUNT_ID_NONE);
    CHECK(fam && fam->unread == 5 && fam->unread_mention && !fam->is_muted, "a mention on either account shows, and a chat muted on one account only still notifies");
    const Chat *dad = row(&list, DAD, ACCOUNT_ID_NONE);
    CHECK(dad && dad->accounts == 1u && dad->account == MAIN, "a contact on one account is that account's row");
    CHECK(list.count == 3 && list.rows[0].is_pinned && strcmp(list.rows[1].jid, DAD) == 0, "pinned chats come first, then the newest");

    s_mom.send_account = WORK;
    unified_chat_list_build(&list, sources, 2, &rules);
    mom = row(&list, MOM, ACCOUNT_ID_NONE);
    CHECK(mom && mom->account == WORK, "a contact with a sending account of their own acts through it");
    s_mom.send_account = SIDE;
    unified_chat_list_build(&list, sources, 2, &rules);
    mom = row(&list, MOM, ACCOUNT_ID_NONE);
    CHECK(mom && mom->account == MAIN, "one that names an account without the chat falls back to the primary");
    s_mom.send_account = ACCOUNT_ID_NONE;
    rules.primary = SIDE;
    unified_chat_list_build(&list, sources, 2, &rules);
    mom = row(&list, MOM, ACCOUNT_ID_NONE);
    CHECK(mom && mom->account == WORK, "with no primary that has the chat, the account they wrote to last sends");
    rules.primary = MAIN;

    s_mom.merge = CHAT_MERGE_NEVER;
    unified_chat_list_build(&list, sources, 2, &rules);
    CHECK(list.count == 4 && rows_for(&list, MOM) == 2 && rows_for(&list, FAM) == 1, "a contact set apart has a row for each account, the others still merge");
    const Chat *mom_main = row(&list, MOM, MAIN), *mom_work = row(&list, MOM, WORK);
    CHECK(mom_main && mom_work && mom_main->unread == 2 && mom_work->unread == 3 && mom_main->accounts == 1u && mom_work->accounts == 2u,
          "each row keeps its own account's count and says whose it is");
    CHECK(unified_chat_list_find(&list, WORK, MOM) == mom_work && unified_chat_list_find(&list, MAIN, MOM) == mom_main,
          "a row is found by its contact and account");
    CHECK(unified_chat_list_find(&list, SIDE, MOM) == NULL, "and not for an account that has no row for them");
    CHECK(unified_chat_list_find(&list, WORK, FAM) == row(&list, FAM, ACCOUNT_ID_NONE), "a merged row is found for any account in it");

    rules.merge_setting = 0;
    s_mom.merge = CHAT_MERGE_FOLLOW;
    s_fam.merge = CHAT_MERGE_ALWAYS;
    unified_chat_list_build(&list, sources, 2, &rules);
    CHECK(list.count == 4 && rows_for(&list, MOM) == 2 && rows_for(&list, FAM) == 1, "with merging off, only a contact set to always merge is one row");

    rules.merge_setting = 1;
    rules.only = WORK;
    unified_chat_list_build(&list, sources, 2, &rules);
    CHECK(list.count == 2 && row(&list, MOM, WORK) && row(&list, FAM, WORK) && !row(&list, DAD, ACCOUNT_ID_NONE),
          "showing one account lists only its chats");
    CHECK(row(&list, MOM, WORK)->unread == 3 && row(&list, MOM, WORK)->accounts == 2u, "each as that account has it");

    rules.only = ACCOUNT_ID_NONE;
    ChatSource empty[] = { { MAIN, NULL, 0 }, { WORK, NULL, 0 } };
    unified_chat_list_build(&list, empty, 2, &rules);
    CHECK(list.count == 0, "accounts with no chats make an empty list");
    unified_chat_list_free(&list);
}

int main(void) {
    test_policies();
    test_list();
    if (failures == 0) printf("ok: several accounts' chats are listed together, a contact on more than one merges or stays apart as chosen, and the right account sends\n");
    return failures != 0;
}
