# stm32f4_ros2

STM32F407VET6 + FreeRTOS + Jazzy micro-ROS，公共应用位于 ../firmware/app，STM32 适配代码位于本工程 Src。两个平台编译同一份应用源码。

- USART1：PA9 TX、PA10 RX，115200 8N1；与 USB-UART 交叉接线并共地，3.3 V 电平。
- RX：DMA2 Stream2 Channel4 循环模式，DMA 半满/全满与 IDLE 接收，4 KiB StreamBuffer。
- HAL tick 使用 TIM14，FreeRTOS 使用 SysTick；HSI 16 MHz。
- heap_4 为 64 KiB，micro-ROS 任务栈 16 KiB。
- 节点 /stm32/stm32f407，heartbeat 每秒发布 Int32，command 原值回传 echo；Agent 断线后重建实体。
- 默认 ROS_DOMAIN_ID=0。修改：`cmake --preset Release -DROS_DOMAIN_ID=1`，WSL 设置相同域。

修改节点/收发/重连逻辑请编辑 ../firmware/app/micro_ros.c；心跳与超时参数编辑 ../firmware/app/micro_ros_config.h。本工程 Src/micro_ros_platform.c 只提供 STM32 初始化、配置、时间/延时及传输接口。

Windows/CLion 打开本目录：

```powershell
cmake --preset Release
cmake --build --preset Release
# ST-LINK 连接后烧录：
cmake --build build/Release --target flash_swd
```

产物：build/Release/stm32f4_ros2.elf、.bin、.hex。链接起点 0x08000000，不适用于旧 bootloader 的 0x08008000 应用布局。

现有 Jazzy libmicroros.a 及配套头文件已移到 Middlewares/Third_Party/micro_ros/lib/jazzy，CMake 从这里链接。WSL 生成脚本输出到此目录。重新生成时在本目录的 WSL 终端执行：

```bash
bash ../Tools/scripts/setup_wsl.sh jazzy
bash scripts/build_micro_ros.sh jazzy
```

在 mcu_ros2 目录启动共用 Agent：

```bash
export ROS_DOMAIN_ID=0
bash Tools/scripts/agent.sh /dev/ttyUSB0 jazzy
```

ROS 2 终端先加载 /opt/ros/jazzy/setup.bash，再订阅 /stm32/heartbeat、/stm32/echo，向 /stm32/command 发送 std_msgs/msg/Int32。

公共逻辑抽离后完整 Release 编译通过：Flash 83,360 B，SRAM 92,824 B。原工作区 build 仅用于保留旧 Agent 及诊断产物。

stm32f4_ros2.ioc 尚未同步手写 UART/RTOS 配置；重新生成 CubeMX 前需同步并检查 main、IRQ、TIM14，避免覆盖集成代码。

来源：[STM32 工具](https://github.com/micro-ROS/micro_ros_stm32cubemx_utils)、[参考教程](https://www.ncnynl.com/archives/202309/6034.html)。共用 Agent 流程见 [总说明](../README.md)。

库按 ROS 发行版保存，默认 MICRO_ROS_DISTRO=jazzy。生成其他发行版后使用 cmake --preset Release -DMICRO_ROS_DISTRO=<发行版>。自定义库目录可通过 MICROROS_ROOT 指定，目录需包含 libmicroros.a 和 include/rcl/rcl.h。

micro-ROS 与 FreeRTOS 一样作为第三方依赖放在 Middlewares/Third_Party 下。micro_ros/tools 保留官方 micro_ros_stm32cubemx_utils 的源码、生成配置、许可证和来源记录；micro_ros/lib/<发行版> 保存当前平台编译后的 libmicroros.a 及配套 include。根目录不再单独放工具仓库和 lib。
