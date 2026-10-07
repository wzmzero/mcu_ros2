#include "micro_ros.h"
#include "micro_ros_platform.h"
#include "micro_ros_config.h"
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <std_msgs/msg/int32.h>
#include <uxr/client/transport.h>
#include <limits.h>
#include <string.h>

#include <rmw_microxrcedds_c/config.h>
#ifndef RMW_UXRCE_TRANSPORT_CUSTOM
#error "Build micro-ROS with custom transport enabled."
#endif

/* Visible in debugger without emitting text on the ROS serial port. */
volatile uint32_t micro_ros_connections, micro_ros_commands, micro_ros_errors;
static rcl_allocator_t ros_allocator;
static micro_ros_platform_config_t platform;
static rclc_support_t support;
static rcl_node_t node;
static rcl_publisher_t heartbeat_publisher, echo_publisher;
static rcl_subscription_t command_subscription;
static rclc_executor_t executor;
static rcl_init_options_t options;
static std_msgs__msg__Int32 command, heartbeat;
static bool has_support, has_node, has_heartbeat, has_echo, has_subscription, has_executor, has_options;

static void command_callback(const void *message)
{
    ++micro_ros_commands;
    if (rcl_publish(&echo_publisher, message, NULL) != RCL_RET_OK) {
        ++micro_ros_errors;
        rcl_reset_error();
    }
}
static void cleanup_result(rcl_ret_t result)
{
    if (result != RCL_RET_OK) {
        ++micro_ros_errors;
        rcl_reset_error();
    }
}
static void destroy_entities(void)
{
    if (has_support) rmw_uros_set_context_entity_destroy_session_timeout(
        rcl_context_get_rmw_context(&support.context), 0);
    if (has_executor) { cleanup_result(rclc_executor_fini(&executor)); has_executor = false; }
    if (has_subscription) { cleanup_result(rcl_subscription_fini(&command_subscription, &node)); has_subscription = false; }
    if (has_echo) { cleanup_result(rcl_publisher_fini(&echo_publisher, &node)); has_echo = false; }
    if (has_heartbeat) { cleanup_result(rcl_publisher_fini(&heartbeat_publisher, &node)); has_heartbeat = false; }
    if (has_node) { cleanup_result(rcl_node_fini(&node)); has_node = false; }
    if (has_support) { cleanup_result(rclc_support_fini(&support)); has_support = false; }
    if (has_options) { cleanup_result(rcl_init_options_fini(&options)); has_options = false; }
    rcl_reset_error();
}
static bool create_entities(void)
{
    node = rcl_get_zero_initialized_node();
    heartbeat_publisher = rcl_get_zero_initialized_publisher();
    echo_publisher = rcl_get_zero_initialized_publisher();
    command_subscription = rcl_get_zero_initialized_subscription();
    executor = rclc_executor_get_zero_initialized_executor();
    options = rcl_get_zero_initialized_init_options();
    memset(&support, 0, sizeof(support));
    if (rcl_init_options_init(&options, ros_allocator) != RCL_RET_OK) return false;
    has_options = true;
    if (rcl_init_options_set_domain_id(&options, platform.domain_id) != RCL_RET_OK) return false;
    if (platform.client_key) {
        rmw_init_options_t *rmw_options = rcl_init_options_get_rmw_init_options(&options);
        if (!rmw_options || rmw_uros_options_set_client_key(platform.client_key, rmw_options) != RMW_RET_OK)
            return false;
    }
    rcl_ret_t result = rclc_support_init_with_options(&support, 0, NULL, &options, &ros_allocator);
    has_support = rcl_context_is_valid(&support.context);
    if (result != RCL_RET_OK) return false;
    if (rclc_node_init_default(&node, platform.node_name, platform.node_namespace, &support) != RCL_RET_OK) return false;
    has_node = true;
    if (rclc_publisher_init_default(&heartbeat_publisher, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "heartbeat") != RCL_RET_OK) return false;
    has_heartbeat = true;
    if (rclc_publisher_init_default(&echo_publisher, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "echo") != RCL_RET_OK) return false;
    has_echo = true;
    if (rclc_subscription_init_default(&command_subscription, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "command") != RCL_RET_OK) return false;
    has_subscription = true;
    if (rclc_executor_init(&executor, &support.context, 1, &ros_allocator) != RCL_RET_OK) return false;
    has_executor = true;
    if (rclc_executor_add_subscription(&executor, &command_subscription, &command,
        command_callback, ON_NEW_DATA) != RCL_RET_OK) return false;
    ++micro_ros_connections;
    return true;
}
void micro_ros_task(void *argument)
{
    (void)argument;
    micro_ros_platform_init();
    platform = micro_ros_platform_config();
    ros_allocator = rcl_get_default_allocator();
    if (rmw_uros_set_custom_transport(platform.transport_framing, NULL, micro_ros_transport_open, micro_ros_transport_close,
        micro_ros_transport_write, micro_ros_transport_read) != RMW_RET_OK) micro_ros_platform_panic();

    for (;;) {
        while (rmw_uros_ping_agent(MICRO_ROS_WAIT_PING_MS, 1) != RMW_RET_OK) micro_ros_platform_delay(MICRO_ROS_RECONNECT_MS);
        if (!create_entities()) {
            ++micro_ros_errors;
            destroy_entities();
            micro_ros_platform_delay(MICRO_ROS_RECONNECT_MS);
            continue;
        }
        uint32_t last_ping = micro_ros_platform_millis(), last_publish = last_ping;
        for (;;) {
            uint32_t now = micro_ros_platform_millis();
            if ((uint32_t)(now - last_ping) >= MICRO_ROS_PING_PERIOD_MS) {
                last_ping = now;
                if (rmw_uros_ping_agent(MICRO_ROS_CONNECTED_PING_MS, 1) != RMW_RET_OK) break;
            }
            rcl_ret_t result = rclc_executor_spin_some(&executor, RCL_MS_TO_NS(MICRO_ROS_SPIN_MS));
            if (result != RCL_RET_OK && result != RCL_RET_TIMEOUT) break;
            if ((uint32_t)(now - last_publish) >= MICRO_ROS_HEARTBEAT_MS) {
                last_publish = now;
                heartbeat.data = heartbeat.data == INT32_MAX ? 0 : heartbeat.data + 1;
                if (rcl_publish(&heartbeat_publisher, &heartbeat, NULL) != RCL_RET_OK) {
                    ++micro_ros_errors;
                    rcl_reset_error();
                }
            }
            micro_ros_platform_delay(MICRO_ROS_LOOP_DELAY_MS);
        }
        destroy_entities();
        micro_ros_platform_delay(MICRO_ROS_RECONNECT_MS);
    }
}
