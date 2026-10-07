#ifndef MICRO_ROS_CONFIG_H
#define MICRO_ROS_CONFIG_H
/* Shared application timing. Both platforms compile this same header. */
#define MICRO_ROS_HEARTBEAT_MS 1000U
#define MICRO_ROS_PING_PERIOD_MS 1000U
#define MICRO_ROS_RECONNECT_MS 500U
#define MICRO_ROS_WAIT_PING_MS 200
#define MICRO_ROS_CONNECTED_PING_MS 100
#define MICRO_ROS_SPIN_MS 10
#define MICRO_ROS_LOOP_DELAY_MS 1U
#endif
