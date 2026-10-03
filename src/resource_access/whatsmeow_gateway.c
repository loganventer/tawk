#include "resource_access/whatsmeow_gateway.h"

#ifdef APP_WITH_WHATSMEOW

#include "resource_access/json_protocol.h"
#include "utilities/log.h"

#include <pthread.h>
#include <stdlib.h>

#include "libtawkwm.h"

/* Go calls back without a `self`, so each gateway registers the queue its
 * events go to under the handle it gave its Go session. Handles count up and
 * are never given twice in one run. */
#define SINKS 16

typedef struct Sink {
    int         handle;         /* 0: free */
    EventQueue *events;
} Sink;

static Sink s_sinks[SINKS];
static int s_next_handle = 1;
static pthread_mutex_t s_sink_lock = PTHREAD_MUTEX_INITIALIZER;

/* Takes a handle for `events`; 0 when every place is taken. */
static int sink_register(EventQueue *events) {
    int handle = 0;
    pthread_mutex_lock(&s_sink_lock);
    for (int i = 0; i < SINKS && !handle; i++) {
        if (s_sinks[i].handle) continue;
        handle = s_next_handle++;
        s_sinks[i] = (Sink){ handle, events };
    }
    pthread_mutex_unlock(&s_sink_lock);
    return handle;
}

static void sink_release(int handle) {
    pthread_mutex_lock(&s_sink_lock);
    for (int i = 0; i < SINKS; i++) {
        if (s_sinks[i].handle == handle) s_sinks[i] = (Sink){ 0, NULL };
    }
    pthread_mutex_unlock(&s_sink_lock);
}

/* Called from Go (any goroutine) with one protocol line of the session `handle`. */
void tawk_wm_emit(int handle, char *line) {
    Event evt;
    if (json_protocol_decode(line, &evt) != 0) return;
    EventQueue *queue = NULL;
    pthread_mutex_lock(&s_sink_lock);
    for (int i = 0; i < SINKS && !queue; i++) {
        if (s_sinks[i].handle == handle) queue = s_sinks[i].events;
    }
    pthread_mutex_unlock(&s_sink_lock);
    if (!queue || event_queue_push(queue, &evt) != 0) event_dispose(&evt);
}

typedef struct Whatsmeow {
    GatewayOptions   options;
    int              handle;      /* names this gateway's session to the Go side */
    int              started;
    IProfileEditor   editor;      /* views of this gateway, handed out by the accessors */
    IStatusPublisher publisher;
} Whatsmeow;

static Whatsmeow *ctx_of(IMessageGateway *self) { return (Whatsmeow *)self->ctx; }

/* `owner` is the ctx of the gateway or of one of its views: all are the gateway. */
static int send_json(void *owner, char *json) {
    if (!json) return -1;
    int rc = TawkWmCommand(((Whatsmeow *)owner)->handle, json);
    free(json);
    return rc;
}

static int gw_start(IMessageGateway *self) {
    Whatsmeow *wm = ctx_of(self);
    if (wm->started) return 0;
    char *init = json_protocol_encode_init(wm->options.auth_dir, wm->options.media_dir, wm->options.log_dir, wm->options.debug);
    int rc = init ? TawkWmInit(wm->handle, init) : -1;
    free(init);
    wm->started = rc == 0;
    if (rc != 0) LOG_ERROR("whatsmeow init failed");
    return rc;
}

static void gw_stop(IMessageGateway *self) {
    Whatsmeow *wm = ctx_of(self);
    if (wm->started) TawkWmShutdown(wm->handle);
    wm->started = 0;
}

static int gw_connect(IMessageGateway *self) { return send_json(self->ctx, json_protocol_encode_connect()); }
static int gw_reconnect(IMessageGateway *self) { return send_json(self->ctx, json_protocol_encode_reconnect()); }
static int gw_send_text(IMessageGateway *self, const char *jid, const OutgoingText *text, const char *id) {
    return send_json(self->ctx, json_protocol_encode_send(jid, text, id));
}
static int gw_reject_call(IMessageGateway *self, const char *from, const char *call_id) {
    return send_json(self->ctx, json_protocol_encode_reject_call(from, call_id));
}

static int gw_request_profile(IMessageGateway *self, const char *jid) {
    return send_json(self->ctx, json_protocol_encode_profile(jid));
}

static int gw_request_picture(IMessageGateway *self, const char *jid, int full) {
    return send_json(self->ctx, json_protocol_encode_picture(jid, full));
}

static int gw_set_blocked(IMessageGateway *self, const char *jid, int blocked) {
    return send_json(self->ctx, json_protocol_encode_block(jid, blocked));
}

static int gw_delete_chat(IMessageGateway *self, const DeleteRequest *r) {
    return send_json(self->ctx, json_protocol_encode_delete_chat(r));
}

static int gw_delete(IMessageGateway *self, const DeleteRequest *r) {
    return send_json(self->ctx, json_protocol_encode_delete(r));
}

static int gw_edit(IMessageGateway *self, const char *jid, const char *id, const char *text) {
    return send_json(self->ctx, json_protocol_encode_edit(jid, id, text));
}
static int gw_react(IMessageGateway *self, const ReactionTarget *t, const char *emoji) {
    return send_json(self->ctx, json_protocol_encode_react(t, emoji));
}
static int gw_typing(IMessageGateway *self, const char *jid, const char *state) {
    return send_json(self->ctx, json_protocol_encode_typing(jid, state));
}
static int gw_subscribe(IMessageGateway *self, const char *jid) {
    return send_json(self->ctx, json_protocol_encode_subscribe(jid));
}
static int gw_request_older(IMessageGateway *self, const HistoryAnchor *a, int count) {
    return send_json(self->ctx, json_protocol_encode_history(a, count));
}
static int gw_presence(IMessageGateway *self, int available) {
    return send_json(self->ctx, json_protocol_encode_presence(available));
}
static int gw_send_voice(IMessageGateway *self, const char *jid, const char *path, int seconds, const char *id) {
    return send_json(self->ctx, json_protocol_encode_send_voice(jid, path, seconds, id));
}
static int gw_send_media(IMessageGateway *self, const char *jid, const OutgoingMedia *media, const char *id) {
    return send_json(self->ctx, json_protocol_encode_send_media(jid, media, id));
}

static int gw_forward_media(IMessageGateway *self, const char *jid, const char *ref, int score, const char *id) {
    return send_json(self->ctx, json_protocol_encode_forward_media(jid, ref, score, id));
}
static int gw_pair(IMessageGateway *self, const char *phone) { return send_json(self->ctx, json_protocol_encode_pair(phone)); }
static int gw_qr(IMessageGateway *self) { return send_json(self->ctx, json_protocol_encode_qr()); }
static int gw_download(IMessageGateway *self, const char *id, const char *ref, int max_mb) {
    return send_json(self->ctx, json_protocol_encode_download(id, ref, max_mb));
}
static int gw_read(IMessageGateway *self, const ReadRequest *r) { return send_json(self->ctx, json_protocol_encode_read(r)); }
static int gw_logout(IMessageGateway *self) { return send_json(self->ctx, json_protocol_encode_logout()); }

static int ed_set_name(IProfileEditor *self, const char *name) {
    return send_json(self->ctx, json_protocol_encode_set_name(name));
}
static int ed_set_about(IProfileEditor *self, const char *text) {
    return send_json(self->ctx, json_protocol_encode_set_about(text));
}
static int ed_set_picture(IProfileEditor *self, const char *path) {
    return send_json(self->ctx, json_protocol_encode_set_picture(path));
}
static int ed_remove_picture(IProfileEditor *self) { return send_json(self->ctx, json_protocol_encode_remove_picture()); }
static int pub_post(IStatusPublisher *self, const StatusPost *post) {
    return send_json(self->ctx, json_protocol_encode_post_status(post));
}

static void gw_destroy(IMessageGateway *self) {
    if (!self) return;
    gw_stop(self);
    sink_release(ctx_of(self)->handle);
    free(self->ctx);
    free(self);
}

IMessageGateway *whatsmeow_gateway_create(const GatewayOptions *options, EventQueue *events) {
    IMessageGateway *gw = calloc(1, sizeof(*gw));
    Whatsmeow *wm = calloc(1, sizeof(*wm));
    if (!gw || !wm) { free(gw); free(wm); return NULL; }
    wm->options = *options;
    wm->editor = (IProfileEditor){ wm, ed_set_name, ed_set_about, ed_set_picture, ed_remove_picture };
    wm->publisher = (IStatusPublisher){ wm, pub_post };
    wm->handle = sink_register(events);
    if (!wm->handle) { free(gw); free(wm); return NULL; }
    gw->ctx = wm;
    gw->start = gw_start;
    gw->connect = gw_connect;
    gw->reconnect = gw_reconnect;
    gw->stop = gw_stop;
    gw->send_text = gw_send_text;
    gw->react = gw_react;
    gw->edit = gw_edit;
    gw->delete_message = gw_delete;
    gw->delete_chat = gw_delete_chat;
    gw->request_profile = gw_request_profile;
    gw->request_picture = gw_request_picture;
    gw->set_blocked = gw_set_blocked;
    gw->reject_call = gw_reject_call;
    gw->typing = gw_typing;
    gw->subscribe = gw_subscribe;
    gw->presence = gw_presence;
    gw->request_older = gw_request_older;
    gw->send_voice = gw_send_voice;
    gw->send_media = gw_send_media;
    gw->forward_media = gw_forward_media;
    gw->request_pairing_code = gw_pair;
    gw->request_qr = gw_qr;
    gw->download_media = gw_download;
    gw->mark_read = gw_read;
    gw->logout = gw_logout;
    gw->destroy = gw_destroy;
    return gw;
}

IProfileEditor *whatsmeow_gateway_profile_editor(IMessageGateway *gateway) {
    return gateway ? &ctx_of(gateway)->editor : NULL;
}

IStatusPublisher *whatsmeow_gateway_status_publisher(IMessageGateway *gateway) {
    return gateway ? &ctx_of(gateway)->publisher : NULL;
}

#else

IMessageGateway *whatsmeow_gateway_create(const GatewayOptions *options, EventQueue *events) {
    (void)options;
    (void)events;
    return NULL;
}

IProfileEditor *whatsmeow_gateway_profile_editor(IMessageGateway *gateway) { (void)gateway; return NULL; }
IStatusPublisher *whatsmeow_gateway_status_publisher(IMessageGateway *gateway) { (void)gateway; return NULL; }

#endif
