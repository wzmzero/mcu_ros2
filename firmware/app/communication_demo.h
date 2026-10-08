#ifndef COMMUNICATION_DEMO_H
#define COMMUNICATION_DEMO_H
#include <stdbool.h>
#include <stdint.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

/* Six executor handles in addition to the base command subscription. */
#define COMMUNICATION_DEMO_HANDLES 6
bool communication_demo_create(rcl_node_t *node, rclc_support_t *support,
                               const char *peer_namespace);
bool communication_demo_attach(rclc_executor_t *executor);
bool communication_demo_step(uint32_t now);
void communication_demo_destroy(rcl_node_t *node);
#endif
