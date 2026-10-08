# 公共固件

两板原生 topic 回环、service、action 和传感器消息选择见 [COMMUNICATION.md](COMMUNICATION.md)。可通过各项目 menuconfig 启用，业务实现位于 `app/communication_demo.c`。

参考 mcu_test/firmware 的共享方式：STM32 和 ESP32 的构建同时 include cmake/firmware.cmake，直接编译这里的一份源码。

- app/micro_ros.c：ROS 节点、heartbeat、command/echo、Agent 检测、实体销毁和重连。
- app/micro_ros_config.h：心跳、检测、超时和重连等待参数。
- core/micro_ros_platform.h：公共平台接口；不引入 MCU、ESP-IDF 或 RTOS 头文件。
- transport/udp_transport.c：可复用 UDP Socket 回调；ESP32 使用 lwIP，Linux 软件测试使用 BSD Socket。串口启用 framing，UDP 关闭 framing，业务逻辑共用。
- cmake/firmware.cmake：公共源文件及头文件目录清单，新增公共代码在这里登记。

各平台在自己的 Src/micro_ros_platform.c 中实现接口。硬件启动、串口 DMA、USB、内存分配和系统时钟放在平台目录。公共业务只改这里，随后分别构建两个 MCU。

本目录只放通用代码。平台编译后的 micro-ROS 库与配套头文件分别保存在 stm32f4_ros2/Middlewares/Third_Party/micro_ros/lib/<发行版> 和 esp32s3_ros2/lib/micro_ros/<发行版>；不在 firmware 中存放 ARM 或 Xtensa 库。
