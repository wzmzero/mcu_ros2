# mcu_ros2

两个与 micro-ROS 强耦合的 MCU 构建工程，ROS 2 发行版统一为 Jazzy。参考 mcu_test 的公共源码引用方式：公共代码只维护 firmware 一份，各 MCU 的 Src 保留平台适配，上位机和测试工具统一放在 Tools。开发时以本目录作为工程根目录。

```
mcu_ros2/
  firmware/
    app/                 公共 micro-ROS 应用及参数
    core/                公共平台接口
    transport/           公共 UDP Socket 传输
    cmake/firmware.cmake  公共源码清单，两个平台同时引用
  stm32f4_ros2/
    Src/                 启动、UART DMA、FreeRTOS 内存/时间及平台适配
    Core/ Drivers/ Middlewares/ cmake/
    Middlewares/Third_Party/micro_ros/
      tools/             官方 STM32 移植及库生成工具
      lib/jazzy/         ARM 静态库和配套 include
    scripts/             STM32 静态库生成
  esp32s3_ros2/
    Src/                 app_main、ESP-IDF 平台适配、USB/UART/Wi-Fi 网络
    components/micro_ros_espidf_component/
    scripts/             WSL 静态库生成及 Windows 库匹配检查
    cmake/               ESP32 库构建配置导出及链接
    lib/micro_ros/jazzy/  Xtensa 静态库和配套 include，构建后生成
    sdkconfig.defaults
    app-colcon.meta
    partitions.csv
  Tools/
    scripts/             WSL 依赖安装、Agent 构建、启动及软件测试
    build/               新工具构建缓存与测试结果
```

| 平台 | 工具链 | 默认通信 | 心跳话题 |
| --- | --- | --- | --- |
| STM32F407 | ARM GCC + CMake | USART1 PA9/PA10，115200 | /stm32/heartbeat |
| ESP32-S3 | ESP-IDF | 原生 USB Serial/JTAG | /esp32s3/heartbeat |

两板的 command、echo 都为 std_msgs/msg/Int32，位于各自命名空间。默认 ROS_DOMAIN_ID=0。两块板同时接入时，分别启动一个串口 Agent，ROS 2 终端可同时看到两组话题。

ESP32 也可选择 Wi-Fi UDP 或 USB RNDIS UDP，配置、构建和测试见 [网络模式说明](esp32s3_ros2/NETWORK.md)。网络 Agent 使用 `bash Tools/scripts/agent.sh udp 8888 jazzy`。


## 公共代码改哪里

- firmware/app/micro_ros.c：节点与实体创建、heartbeat 发布、command/echo、Agent 检测、断线销毁和重连。两个平台直接编译这同一份源码。
- firmware/app/micro_ros_config.h：心跳周期、检测周期、超时和重连等待。修改一次，重新构建两个平台即可生效。
- firmware/core/micro_ros_platform.h：公共应用调用的初始化、时间、延时、配置和传输接口，不包含 STM32、ESP-IDF 或 FreeRTOS 头文件。
- firmware/cmake/firmware.cmake：公共源码清单，两个 MCU 构建都 include 它；新增公共源文件在这里登记。

各平台 Src/micro_ros_platform.c 提供节点名、命名空间、域和硬件接口实现。STM32 使用 HAL 时钟和 FreeRTOS 自定义分配器；ESP32 使用 esp_timer、系统分配器及随机客户端 key。UART DMA、USB 驱动、任务创建与栈单位留在平台目录。

heartbeat、command、echo 使用相对话题名，由平台命名空间展开，原来的 /stm32/* 与 /esp32s3/* 路径保持一致。firmware 只放通用代码。ARM 与 Xtensa 的 micro-ROS 静态库及配套头文件分别放到 stm32f4_ros2/Middlewares/Third_Party/micro_ros/lib/jazzy 和 esp32s3_ros2/lib/micro_ros/jazzy，由各自工具链生成和链接。Agent 放在 Tools，固件 elf/bin/hex 仍在各平台 build。

工具使用方法见 [Tools/README.md](Tools/README.md)。

## 构建

STM32：在 stm32f4_ros2 执行 `cmake --preset Release`、`cmake --build --preset Release`。已有 Jazzy 静态库随工程迁移，无需重新生成。

ESP32：与 STM32 相同，可在 WSL 生成库、Windows 构建和烧录应用，不需要在 WSL 安装 ESP-IDF。先在激活的 Windows ESP-IDF 终端执行 `idf.py -DMICROROS_PREPARE=ON reconfigure`，在 WSL 中执行 `bash scripts/build_micro_ros.sh`，再在 Windows 执行 `idf.py -DMICROROS_PREPARE=OFF build`。以后只修改应用代码直接 `idf.py build`。详细命令和依赖见 [ESP32 README](esp32s3_ros2/README.md)。

官方组件使用 POSIX make/colcon 构建静态库，不要求 Docker。Windows + WSL 模式复用已有 Windows SDK，只在 WSL 下载匹配的 Xtensa 工具链。ESP32 的 USB 数据口只用于 XRCE 帧，不使用 idf.py monitor 作为 ROS 接收工具。

## Agent 与收发

在本目录的 WSL 终端执行；设备必须先通过 usbipd attach 到 WSL：

```bash
export ROS_DOMAIN_ID=0
# 按实际设备路径选择一个；同时接两块板时分别在两个终端启动：
bash Tools/scripts/agent.sh /dev/ttyUSB0 jazzy   # STM32 的 USB-UART
bash Tools/scripts/agent.sh /dev/ttyACM0 jazzy   # ESP32 原生 USB
```

新 ROS 2 终端：

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0
ros2 node list
ros2 topic echo /esp32s3/heartbeat
```

另开终端监听 `ros2 topic echo /esp32s3/echo`，再发送：

```bash
ros2 topic pub --once /esp32s3/command std_msgs/msg/Int32 '{data: 123}'
```

echo 应收到 123。每个新终端都需加载 Jazzy 环境并设置同一个域。

## 既有 Agent 缓存

之前已编译的工作区保留在本目录的上一层 `build/micro_ros/jazzy`，因为 colcon 安装文件包含绝对路径，不能直接搬动。Tools 中的脚本自动复用这个工作区；新的 Agent 工作区默认放到 Tools/build/micro_ros；用 `MICRO_ROS_HOST_ROOT` 可指定另一工作区根目录。将整个 mcu_ros2 搬到新位置后，若找不到旧缓存，执行：

```bash
bash Tools/scripts/build_agent.sh jazzy
bash Tools/scripts/test_agent.sh jazzy
```

## 验证状态

公共源码抽离后 STM32 完整 Release 编译链接通过：Flash 83,360 B、SRAM 92,824 B。

2026-10-07：ESP32 Windows + WSL 完整构建通过。WSL 复用 Windows SDK 并生成 1,695 对象的 Xtensa 库，Windows ESP-IDF 6.1 成功链接固件，生成 238,048 B 的 bin；未安装 Linux ESP-IDF。详细记录见 ESP32 README 和 esp32s3_ros2/build/workflow_validation.json。烧录与实板 heartbeat/echo 尚未验证。现有 Agent 的构建及软件 ping/PTY 检查记录保留在旧 build 下。

组件来源：[官方 ESP-IDF 组件 Jazzy 分支](https://github.com/micro-ROS/micro_ros_espidf_component/tree/jazzy)。参考结构：C:\Users\admin\Desktop\mcu_test 的公共源码与平台构建方式。详细配置见两个 MCU 的 README。
