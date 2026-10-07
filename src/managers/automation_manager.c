#include "managers/automation_manager.h"
#include "engines/automation_policy.h"
#include "engines/chat_reference_resolver.h"
#include "engines/confirmation_token.h"
#include "engines/hourly_quota.h"
#include "engines/rate_limiter.h"
#include "engines/recipient_resolver.h"
#include "utilities/log.h"
#include "utilities/str_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_COMMANDS 16
#define MAX_NOTICES  8
#define ADMIN_TOKEN_SIZE 72

struct AutomationManager {
    /* The account being served, and what applies to it in place of the settings. */
    AccountId             account;
    int                   serving;
    char                  access[8];
    char                  self_chats[1024];
    Settings              effective;
    int                   admin_wanted;       /* -1 until said */
    AutomationManagerDeps deps;
    RateLimiter           writes;
    AutomationStatus      status;
    int                   changed;
    AutomationCommand     commands[MAX_COMMANDS];
    int                   command_count;
    HourlyQuota           self_approvals;
    char                  admin_token[ADMIN_TOKEN_SIZE];   /* "" while access is not admin */
    char                  notices[MAX_NOTICES][256];
    int                   notice_count;
};

/* The settings as they apply to the account being served. */
static const Settings *eff(AutomationManager *m) {
    if (!m->serving) return m->deps.settings;
    m->effective = *m->deps.settings;
    str_copy(m->effective.automation_access, sizeof(m->effective.automation_access), m->access);
    str_copy(m->effective.automation_self_chats, sizeof(m->effective.automation_self_chats), m->self_chats);
    return &m->effective;
}

void automation_manager_serve(AutomationManager *m, AccountId account, const char *access, const char *self_chats) {
    m->account = account;
    m->serving = access != NULL;
    str_copy(m->access, sizeof(m->access), access ? access : "");
    str_copy(m->self_chats, sizeof(m->self_chats), self_chats ? self_chats : "");
}

void automation_manager_admin_wanted(AutomationManager *m, int wanted) {
    m->admin_wanted = wanted ? 1 : 0;
}

AutomationManager *automation_manager_create(const AutomationManagerDeps *deps) {
    AutomationManager *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->deps = *deps;
    m->admin_wanted = -1;                                   /* not said: the setting decides */
    rate_limiter_init(&m->writes);
    hourly_quota_init(&m->self_approvals);
    return m;
}

static void drop_admin_token(AutomationManager *m) {
    if (m->deps.admin_tokens) m->deps.admin_tokens->remove(m->deps.admin_tokens);
    memset(m->admin_token, 0, sizeof(m->admin_token));
}

void automation_manager_destroy(AutomationManager *m) {
    if (!m) return;
    drop_admin_token(m);                                    /* the token is good for one run of tawk only */
    free(m);
}

int automation_manager_chat_allowed(AutomationManager *m, const Chat *chat) {
    return automation_policy_chat_allowed(eff(m), chat);
}

ChatResolution automation_manager_resolve(AutomationManager *m, const Chat *chats, int count, const char *ref,
                                          int *found, int *candidates, int max, int *candidate_count) {
    return chat_reference_resolve(chats, count, eff(m), ref, found, candidates, max, candidate_count);
}

ChatResolution automation_manager_resolve_recipient(AutomationManager *m, const Chat *chats, int chat_count,
                                                    const Contact *contacts, int contact_count, const char *ref,
                                                    Recipient *found, Recipient *candidates, int max, int *candidate_count) {
    return recipient_resolve(eff(m), chats, chat_count, contacts, contact_count, ref, found, candidates, max, candidate_count);
}

const char *automation_manager_access(AutomationManager *m) {
    const char *a = eff(m)->automation_access;
    return strcmp(a, "admin") == 0 || strcmp(a, "manage") == 0 || strcmp(a, "send") == 0 ? a : "read";
}

int automation_manager_pushes(AutomationManager *m, ControlOrigin origin, int from_me) {
    return automation_policy_pushes(eff(m), origin, from_me);
}

int automation_manager_pushes_event(AutomationManager *m, ControlOrigin origin, LiveKind kind) {
    return automation_policy_pushes_event(eff(m), origin, kind);
}

int automation_manager_presence_lookup(AutomationManager *m, ControlOrigin origin) {
    return automation_policy_presence_lookup(eff(m), origin);
}

const char *automation_manager_disclaimer(AutomationManager *m, ControlOrigin origin, const char *op) {
    return automation_policy_disclaimer(eff(m), origin, op);
}

int automation_manager_setting_changeable(AutomationManager *m, const SettingField *field) {
    (void)m;
    return automation_policy_setting_changeable(field);
}

AutomationVerdict automation_manager_check_write(AutomationManager *m, ControlOrigin origin, WriteKind kind,
                                                int64_t now_ms, int *retry_after_s) {
    AutomationVerdict verdict = automation_policy_write(eff(m), origin, kind);
    if (verdict == AUTOMATION_VERDICT_REFUSE) return verdict;
    if (!rate_limiter_take(&m->writes, eff(m)->automation_rate, now_ms, retry_after_s)) return AUTOMATION_VERDICT_RATE_LIMITED;
    return verdict;
}

void automation_manager_tick(AutomationManager *m) {
    int wanted = m->admin_wanted >= 0 ? m->admin_wanted : strcmp(m->deps.settings->automation_access, "admin") == 0;
    int admin = wanted && m->deps.admin_tokens != NULL;
    if (!admin) {
        if (m->admin_token[0]) drop_admin_token(m);
        return;
    }
    if (m->admin_token[0]) return;
    char a[40], b[40];
    if (confirmation_token_generate(a, sizeof(a)) != 0 || confirmation_token_generate(b, sizeof(b)) != 0) return;
    char token[ADMIN_TOKEN_SIZE];
    snprintf(token, sizeof(token), "%s%s", a, b);
    if (m->deps.admin_tokens->save(m->deps.admin_tokens, token) != 0) return;   /* tried again on the next tick */
    str_copy(m->admin_token, sizeof(m->admin_token), token);
    LOG_INFO("automation: access is admin; a new admin token was written");
}

/* Compares every byte whatever they are, so the time taken says nothing about where they differ. */
static int same_token(const char *have, const char *shown) {
    size_t n = strlen(have), k = shown ? strlen(shown) : 0;
    unsigned char diff = (unsigned char)(n != k);
    for (size_t i = 0; i < n; i++) diff |= (unsigned char)(have[i] ^ (i < k ? shown[i] : 0));
    return n > 0 && diff == 0;
}

SelfApprovalVerdict automation_manager_self_approve(AutomationManager *m, const char *op, const Chat *chat,
                                                    const char *token, int64_t now_ms, int *retry_after_s) {
    SelfApprovalVerdict verdict = automation_policy_self_approval(eff(m), op, chat);
    if (verdict == SELF_APPROVAL_OFF) return verdict;
    if (!same_token(m->admin_token, token)) return SELF_APPROVAL_BAD_TOKEN;
    if (verdict != SELF_APPROVAL_ALLOW) return verdict;
    if (!hourly_quota_take(&m->self_approvals, eff(m)->automation_self_per_hour, now_ms, retry_after_s)) return SELF_APPROVAL_RATE_LIMITED;
    return SELF_APPROVAL_ALLOW;
}

void automation_manager_notice(AutomationManager *m, const char *text) {
    if (m->notice_count >= MAX_NOTICES) {                    /* the oldest gives way */
        memmove(m->notices[0], m->notices[1], sizeof(m->notices[0]) * (MAX_NOTICES - 1));
        m->notice_count--;
    }
    str_copy(m->notices[m->notice_count], sizeof(m->notices[0]), text);
    str_strip_controls(m->notices[m->notice_count++]);
    m->changed = 1;
}

int automation_manager_take_notice(AutomationManager *m, char *out, unsigned long size) {
    if (m->notice_count == 0) return 0;
    str_copy(out, size, m->notices[0]);
    memmove(m->notices[0], m->notices[1], sizeof(m->notices[0]) * (size_t)(--m->notice_count));
    return 1;
}

ApprovalRisk automation_manager_risk(AutomationManager *m, const char *op, WriteKind kind) {
    (void)m;
    return automation_policy_risk(op, kind);
}

int64_t automation_manager_answer_window_ms(AutomationManager *m, ApprovalRisk risk) {
    (void)m;
    return risk == APPROVAL_RISK_HIGH ? 2 * 60 * 1000 : 5 * 60 * 1000;
}

void automation_manager_command(AutomationManager *m, AutomationCommandKind kind, int conn) {
    if (m->command_count >= MAX_COMMANDS) return;
    m->commands[m->command_count++] = (AutomationCommand){ kind, conn };
}

int automation_manager_take_command(AutomationManager *m, AutomationCommand *out) {
    if (m->command_count == 0) return 0;
    *out = m->commands[0];
    memmove(m->commands, m->commands + 1, (size_t)--m->command_count * sizeof(m->commands[0]));
    return 1;
}

int automation_manager_new_token(AutomationManager *m, char *out, unsigned long size) {
    (void)m;
    return confirmation_token_generate(out, size);
}

void automation_manager_record(AutomationManager *m, ControlOrigin origin, const char *client, const char *op,
                               const char *chat_jid, const char *summary, AutomationOutcome outcome) {
    AutomationEntry e;
    memset(&e, 0, sizeof(e));
    e.at = (int64_t)time(NULL);
    e.origin = origin;
    e.outcome = outcome;
    str_copy(e.client, sizeof(e.client), client ? client : "");
    str_copy(e.op, sizeof(e.op), op ? op : "");
    str_copy(e.chat_jid, sizeof(e.chat_jid), chat_jid ? chat_jid : "");
    str_copy(e.summary, sizeof(e.summary), summary ? summary : "");
    e.account = m->account;
    str_strip_controls(e.summary);
    if (m->deps.log && m->deps.log->append(m->deps.log, &e) != 0) LOG_WARN("automation: could not log %s", e.op);
    LOG_INFO("automation: %s %s %s %s", control_origin_name(origin), e.op, e.chat_jid, automation_outcome_name(outcome));
    m->changed = 1;
}

int automation_manager_recent(AutomationManager *m, int limit, AutomationEntry **out, int *count) {
    *out = NULL;
    *count = 0;
    return m->deps.log ? m->deps.log->recent(m->deps.log, limit, out, count) : -1;
}

void automation_manager_set_status(AutomationManager *m, const AutomationStatus *status) {
    if (memcmp(&m->status, status, sizeof(*status)) != 0) m->changed = 1;
    m->status = *status;
}

const AutomationStatus *automation_manager_status(AutomationManager *m) { return &m->status; }

int automation_manager_take_changed(AutomationManager *m) {
    int c = m->changed;
    m->changed = 0;
    return c;
}
