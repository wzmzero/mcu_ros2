#include "communication_demo.h"
#ifdef MICRO_ROS_COMM_DEMO
#include "micro_ros_platform.h"
#include <rcl/error_handling.h>
#include <rclc/action_client.h>
#include <rclc/action_server.h>
#include <example_interfaces/action/fibonacci.h>
#include <example_interfaces/srv/add_two_ints.h>
#include <std_msgs/msg/int32.h>
#include <std_msgs/msg/int64.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <rmw_microxrcedds_c/config.h>

#if RMW_UXRCE_MAX_PUBLISHERS < 10 || RMW_UXRCE_MAX_SUBSCRIPTIONS < 5 || RMW_UXRCE_MAX_SERVICES < 4 || RMW_UXRCE_MAX_CLIENTS < 4
#error "Rebuild the micro-ROS library with the repository colcon.meta before enabling the communication demo."
#endif

#define GOALS 2
#define SEQUENCE_CAPACITY 10
#define STEP_MS 200u
#define REQUEST_PERIOD_MS 5000u
#define REQUEST_TIMEOUT_MS 15000u

extern volatile uint32_t micro_ros_errors;
/* Useful when inspecting a board whose console is occupied by a transport. */
volatile uint32_t communication_demo_stage;
volatile uint32_t communication_demo_recoveries;
enum { RECEIVED, ROUNDTRIP, SERVICE_RESULT, ACTION_RESULT, ACTION_FEEDBACK, ACTION_STATUS, PUB_COUNT };
static const char *const publisher_names[PUB_COUNT] = {
    "peer_received", "roundtrip", "service_result", "action_result", "action_feedback", "action_status"
};
static rcl_publisher_t publishers[PUB_COUNT];
static unsigned publisher_count, subscription_count;
static rcl_subscription_t subscriptions[2];
static std_msgs__msg__Int32 peer_messages[2];
static rcl_service_t service;
static rcl_client_t client;
static bool has_service, has_client, has_action_server, has_action_client;
static rclc_action_server_t action_server;
static rclc_action_client_t action_client;
static example_interfaces__srv__AddTwoInts_Request service_request, peer_request;
static example_interfaces__srv__AddTwoInts_Response service_response, peer_response;
static int64_t service_sequence;
static bool service_pending;
static uint32_t last_service, last_action, action_started;
static uint32_t last_peer_heartbeat, last_peer_ack;
static bool peer_seen;
static unsigned action_number;
static bool action_pending, cancel_sent;
static example_interfaces__action__Fibonacci_SendGoal_Request goals[GOALS], peer_goal;
static example_interfaces__action__Fibonacci_FeedbackMessage client_feedback;
static example_interfaces__action__Fibonacci_GetResult_Response client_result;
static int32_t client_feedback_data[SEQUENCE_CAPACITY], client_result_data[SEQUENCE_CAPACITY];
typedef struct {
    rclc_action_goal_handle_t *handle;
    int32_t order, data[SEQUENCE_CAPACITY];
    uint32_t last_step;
    rcl_action_goal_state_t terminal;
    example_interfaces__action__Fibonacci_FeedbackMessage feedback;
    example_interfaces__action__Fibonacci_GetResult_Response result;
} action_job_t;
static action_job_t jobs[GOALS];

static void check(rcl_ret_t result)
{
    if (result != RCL_RET_OK) { ++micro_ros_errors; rcl_reset_error(); }
}
static void publish_int(unsigned publisher, int32_t value)
{
    std_msgs__msg__Int32 message = {.data = value};
    check(rcl_publish(&publishers[publisher], &message, NULL));
}
static void peer_heartbeat(const void *message)
{
    uint32_t now = micro_ros_platform_millis();
    if (!peer_seen) last_peer_ack = now;
    peer_seen = true;
    last_peer_heartbeat = now;
    check(rcl_publish(&publishers[RECEIVED], message, NULL));
}
static void peer_ack(const void *message)
{
    last_peer_ack = micro_ros_platform_millis();
    check(rcl_publish(&publishers[ROUNDTRIP], message, NULL));
}
static void add_two_ints(const void *request, void *response)
{
    const example_interfaces__srv__AddTwoInts_Request *in = request;
    example_interfaces__srv__AddTwoInts_Response *out = response;
    /* Saturate outside the signed 64-bit range instead of invoking C overflow. */
    if (in->b > 0 && in->a > INT64_MAX - in->b) out->sum = INT64_MAX;
    else if (in->b < 0 && in->a < INT64_MIN - in->b) out->sum = INT64_MIN;
    else out->sum = in->a + in->b;
}
static void service_reply(const void *response, rmw_request_id_t *id)
{
    if (!service_pending || id->sequence_number != service_sequence) return;
    service_pending = false;
    std_msgs__msg__Int64 message = {
        .data = ((const example_interfaces__srv__AddTwoInts_Response *)response)->sum
    };
    check(rcl_publish(&publishers[SERVICE_RESULT], &message, NULL));
}
static rcl_ret_t accept_goal(rclc_action_goal_handle_t *handle, void *context)
{
    (void)context;
    const example_interfaces__action__Fibonacci_SendGoal_Request *request =
        (const example_interfaces__action__Fibonacci_SendGoal_Request *)handle->ros_goal_request;
    if (request->goal.order < 2 || request->goal.order > SEQUENCE_CAPACITY)
        return RCL_RET_ACTION_GOAL_REJECTED;
    for (unsigned i = 0; i < GOALS; ++i) {
        action_job_t *job = &jobs[i];
        if (job->handle) continue;
        memset(job, 0, sizeof(*job));
        job->handle = handle;
        job->order = request->goal.order;
        job->data[0] = 0; job->data[1] = 1;
        job->feedback.feedback.sequence.data = job->data;
        job->feedback.feedback.sequence.size = 2;
        job->feedback.feedback.sequence.capacity = SEQUENCE_CAPACITY;
        job->result.result.sequence.data = job->data;
        job->result.result.sequence.capacity = SEQUENCE_CAPACITY;
        job->last_step = micro_ros_platform_millis();
        return RCL_RET_ACTION_GOAL_ACCEPTED;
    }
    return RCL_RET_ACTION_GOAL_REJECTED;
}
static bool accept_cancel(rclc_action_goal_handle_t *handle, void *context)
{
    (void)context;
    for (unsigned i = 0; i < GOALS; ++i)
        if (jobs[i].handle == handle && !jobs[i].terminal) return true;
    return false;
}
static void goal_reply(rclc_action_goal_handle_t *handle, bool accepted, void *context)
{
    (void)handle; (void)context;
    if (!accepted) action_pending = false;
    publish_int(ACTION_STATUS, accepted ? GOAL_STATE_ACCEPTED : GOAL_STATE_UNKNOWN);
}
static void feedback_reply(rclc_action_goal_handle_t *handle, void *message, void *context)
{
    (void)context;
    const example_interfaces__action__Fibonacci_FeedbackMessage *feedback = message;
    publish_int(ACTION_FEEDBACK, (int32_t)feedback->feedback.sequence.size);
    if (peer_goal.goal.order == 10 && feedback->feedback.sequence.size >= 4 && !cancel_sent) {
        rcl_ret_t result = rclc_action_send_cancel_request(handle);
        cancel_sent = result == RCL_RET_OK;
        check(result);
    }
}
static void result_reply(rclc_action_goal_handle_t *handle, void *message, void *context)
{
    (void)handle; (void)context;
    const example_interfaces__action__Fibonacci_GetResult_Response *response = message;
    action_pending = false;
    publish_int(ACTION_STATUS, response->status);
    if (response->result.sequence.size)
        publish_int(ACTION_RESULT, response->result.sequence.data[response->result.sequence.size - 1]);
}
static void cancel_reply(rclc_action_goal_handle_t *handle, bool accepted, void *context)
{
    (void)handle; (void)context;
    if (!accepted) ++micro_ros_errors;
}
static bool peer_name(char *out, size_t capacity, const char *ns, const char *name)
{
    int size = snprintf(out, capacity, "%s/%s", ns, name);
    return size > 0 && (size_t)size < capacity;
}
bool communication_demo_create(rcl_node_t *node, rclc_support_t *support, const char *peer_namespace)
{
    char name[64];
    communication_demo_stage = 1;
    memset(jobs, 0, sizeof(jobs));
    action_pending = service_pending = cancel_sent = false;
    peer_seen = false;
    last_service = last_action = micro_ros_platform_millis();
    last_peer_heartbeat = last_peer_ack = last_service;
    action_number = 0;
    memset(&action_server, 0, sizeof(action_server));
    memset(&action_client, 0, sizeof(action_client));
    for (unsigned i = 0; i < PUB_COUNT; ++i) {
        publishers[i] = rcl_get_zero_initialized_publisher();
        const rosidl_message_type_support_t *type = i == SERVICE_RESULT
            ? ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int64)
            : ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32);
        if (rclc_publisher_init_default(&publishers[i], node, type, publisher_names[i]) != RCL_RET_OK)
            return false;
        ++publisher_count;
    }
    communication_demo_stage = 2;
    const char *const peer_topics[] = {"heartbeat", "peer_received"};
    for (unsigned i = 0; i < 2; ++i) {
        subscriptions[i] = rcl_get_zero_initialized_subscription();
        if (!peer_name(name, sizeof(name), peer_namespace, peer_topics[i]) ||
            rclc_subscription_init_default(&subscriptions[i], node,
                ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), name) != RCL_RET_OK) return false;
        ++subscription_count;
    }
    communication_demo_stage = 3;
    service = rcl_get_zero_initialized_service();
    client = rcl_get_zero_initialized_client();
    if (rclc_service_init_default(&service, node,
        ROSIDL_GET_SRV_TYPE_SUPPORT(example_interfaces, srv, AddTwoInts), "add_two_ints") != RCL_RET_OK)
        return false;
    has_service = true;
    if (!peer_name(name, sizeof(name), peer_namespace, "add_two_ints") ||
        rclc_client_init_default(&client, node,
            ROSIDL_GET_SRV_TYPE_SUPPORT(example_interfaces, srv, AddTwoInts), name) != RCL_RET_OK) return false;
    has_client = true;
    communication_demo_stage = 4;
    if (rclc_action_server_init_default(&action_server, node, support,
        ROSIDL_GET_ACTION_TYPE_SUPPORT(example_interfaces, Fibonacci), "fibonacci") != RCL_RET_OK) return false;
    has_action_server = true;
    communication_demo_stage = 5;
    if (!peer_name(name, sizeof(name), peer_namespace, "fibonacci") ||
        rclc_action_client_init_default(&action_client, node,
            ROSIDL_GET_ACTION_TYPE_SUPPORT(example_interfaces, Fibonacci), name) != RCL_RET_OK) return false;
    has_action_client = true;
    client_feedback.feedback.sequence.data = client_feedback_data;
    client_feedback.feedback.sequence.size = 0;
    client_feedback.feedback.sequence.capacity = SEQUENCE_CAPACITY;
    client_result.result.sequence.data = client_result_data;
    client_result.result.sequence.size = 0;
    client_result.result.sequence.capacity = SEQUENCE_CAPACITY;
    return true;
}
bool communication_demo_attach(rclc_executor_t *executor)
{
    communication_demo_stage = 6;
    if (rclc_executor_add_subscription(executor, &subscriptions[0], &peer_messages[0], peer_heartbeat, ON_NEW_DATA) != RCL_RET_OK ||
        rclc_executor_add_subscription(executor, &subscriptions[1], &peer_messages[1], peer_ack, ON_NEW_DATA) != RCL_RET_OK ||
        rclc_executor_add_service(executor, &service, &service_request, &service_response, add_two_ints) != RCL_RET_OK ||
        rclc_executor_add_client_with_request_id(executor, &client, &peer_response, service_reply) != RCL_RET_OK ||
        rclc_executor_add_action_server(executor, &action_server, GOALS, goals, sizeof(goals[0]), accept_goal, accept_cancel, NULL) != RCL_RET_OK ||
        rclc_executor_add_action_client(executor, &action_client, 1, &client_result, &client_feedback,
            goal_reply, feedback_reply, result_reply, cancel_reply, NULL) != RCL_RET_OK) return false;
    communication_demo_stage = 7;
    return true;
}
bool communication_demo_step(uint32_t now)
{
    for (unsigned i = 0; i < GOALS; ++i) {
        action_job_t *job = &jobs[i];
        if (!job->handle) continue;
        if (job->handle->goal_cancelled) job->terminal = GOAL_STATE_CANCELED;
        size_t size = job->feedback.feedback.sequence.size;
        if (!job->terminal && (uint32_t)(now - job->last_step) >= STEP_MS) {
            job->last_step = now;
            if (size < (size_t)job->order) {
                job->data[size] = job->data[size - 1] + job->data[size - 2];
                job->feedback.feedback.sequence.size = ++size;
            }
            check(rclc_action_publish_feedback(job->handle, &job->feedback));
            if (size == (size_t)job->order) job->terminal = GOAL_STATE_SUCCEEDED;
        }
        if (job->terminal) {
            job->result.result.sequence.size = size;
            rcl_ret_t result = rclc_action_send_result(job->handle, job->terminal, &job->result);
            if (result != RCLC_RET_ACTION_WAIT_RESULT_REQUEST) {
                check(result);
                if (result != RCL_RET_OK) return false;
                job->handle = NULL;
            }
        }
    }
    if ((uint32_t)(now - last_service) >= REQUEST_PERIOD_MS) {
        peer_request.a = 20; peer_request.b = 22;
        rcl_ret_t result = rcl_send_request(&client, &peer_request, &service_sequence);
        service_pending = result == RCL_RET_OK;
        last_service = now;
        check(result);
    }
    if (!action_pending && (uint32_t)(now - last_action) >= REQUEST_PERIOD_MS) {
        /* Alternate completion and cancellation to exercise both MCU clients. */
        peer_goal.goal.order = action_number++ % 2 ? 10 : 6;
        rcl_ret_t result = rclc_action_send_goal_request(&action_client, &peer_goal, NULL);
        action_pending = result == RCL_RET_OK;
        cancel_sent = false;
        last_action = action_started = now;
        check(result);
    }
    /* A lost goal response otherwise permanently occupies the sole client slot. */
    bool lost_goal = action_pending && (uint32_t)(now - action_started) >= REQUEST_TIMEOUT_MS;
    /* Agent ping alone cannot detect an individual stale subscription after a
     * peer reboot. Renew our entities if live peer heartbeats lack roundtrips. */
    bool lost_ack = peer_seen && (uint32_t)(now - last_peer_heartbeat) < 3000u &&
                    (uint32_t)(now - last_peer_ack) >= 10000u;
    if (lost_goal || lost_ack) { ++communication_demo_recoveries; return false; }
    return true;
}
void communication_demo_destroy(rcl_node_t *node)
{
    if (has_action_client) { check(rclc_action_client_fini(&action_client, node)); has_action_client = false; }
    if (has_action_server) { check(rclc_action_server_fini(&action_server, node)); has_action_server = false; }
    if (has_client) { check(rcl_client_fini(&client, node)); has_client = false; }
    if (has_service) { check(rcl_service_fini(&service, node)); has_service = false; }
    while (subscription_count) check(rcl_subscription_fini(&subscriptions[--subscription_count], node));
    while (publisher_count) check(rcl_publisher_fini(&publishers[--publisher_count], node));
    communication_demo_stage = 0;
}
#else
bool communication_demo_create(rcl_node_t *node, rclc_support_t *support, const char *peer_namespace)
{ (void)node; (void)support; (void)peer_namespace; return false; }
bool communication_demo_attach(rclc_executor_t *executor) { (void)executor; return false; }
bool communication_demo_step(uint32_t now) { (void)now; return false; }
void communication_demo_destroy(rcl_node_t *node) { (void)node; }
#endif
