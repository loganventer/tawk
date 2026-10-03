/* Several whatsmeow gateways in one tawk: each has its own session on the
 * Go side, and an event from one session reaches only that gateway's queue. */
#include "resource_access/whatsmeow_gateway.h"
#include "utilities/event_queue.h"

#include <stdio.h>
#include <string.h>

#ifdef APP_WITH_WHATSMEOW

void tawk_wm_emit(int handle, char *line);      /* what the Go side calls */

static int failures = 0;

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", what); failures++; } } while (0)

static int pop_type(EventQueue *queue, EventType *type) {
    Event evt;
    if (event_queue_pop(queue, &evt) != 0) return -1;
    *type = evt.type;
    event_dispose(&evt);
    return 0;
}

static void emit(int handle, const char *line) {
    char copy[160];
    snprintf(copy, sizeof(copy), "%s", line);
    tawk_wm_emit(handle, copy);
}

int main(void) {
    GatewayOptions options;
    memset(&options, 0, sizeof(options));
    EventQueue *first_queue = event_queue_create(8), *second_queue = event_queue_create(8);
    /* Never started, so nothing here links to WhatsApp or writes a login store. */
    IMessageGateway *first = whatsmeow_gateway_create(&options, first_queue);
    IMessageGateway *second = whatsmeow_gateway_create(&options, second_queue);
    CHECK(first && second, "two gateways are made side by side");

    EventType type = EVENT_NONE;
    emit(1, "{\"evt\":\"logged_out\"}");
    CHECK(pop_type(first_queue, &type) == 0 && type == EVENT_AUTH_LOGGED_OUT, "the first session's event reaches the first gateway");
    CHECK(pop_type(second_queue, &type) != 0, "and not the second");
    emit(2, "{\"evt\":\"logged_out\"}");
    CHECK(pop_type(second_queue, &type) == 0 && type == EVENT_AUTH_LOGGED_OUT, "the second session's event reaches the second gateway");
    CHECK(pop_type(first_queue, &type) != 0, "and not the first");
    emit(7, "{\"evt\":\"logged_out\"}");
    CHECK(pop_type(first_queue, &type) != 0 && pop_type(second_queue, &type) != 0, "an event of a session nobody holds goes nowhere");

    first->destroy(first);
    emit(1, "{\"evt\":\"logged_out\"}");
    CHECK(pop_type(first_queue, &type) != 0, "a gateway that is gone receives nothing more");
    emit(2, "{\"evt\":\"logged_out\"}");
    CHECK(pop_type(second_queue, &type) == 0, "while the other carries on");

    /* A gateway made later gets a handle of its own, never one given before. */
    EventQueue *third_queue = event_queue_create(8);
    IMessageGateway *third = whatsmeow_gateway_create(&options, third_queue);
    emit(1, "{\"evt\":\"logged_out\"}");
    CHECK(pop_type(third_queue, &type) != 0, "a new gateway does not inherit a handle that was given up");
    emit(3, "{\"evt\":\"logged_out\"}");
    CHECK(pop_type(third_queue, &type) == 0, "it has the next one");

    second->destroy(second);
    third->destroy(third);
    event_queue_destroy(first_queue);
    event_queue_destroy(second_queue);
    event_queue_destroy(third_queue);
    if (failures == 0) printf("ok: each whatsmeow gateway has its own session, and events reach only the gateway they belong to\n");
    return failures != 0;
}

#else

int main(void) {
    printf("ok: built without whatsmeow, so there are no whatsmeow sessions to tell apart\n");
    return 0;
}

#endif
