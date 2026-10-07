#ifndef MICRO_ROS_H
#define MICRO_ROS_H
#include <stdint.h>
void micro_ros_task(void *argument);
extern volatile uint32_t micro_ros_connections, micro_ros_commands, micro_ros_errors;
#endif
