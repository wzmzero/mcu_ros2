# esp32s3_ros2

两板原生 topic/service/action 及传感器消息说明见 [COMMUNICATION.md](../firmware/COMMUNICATION.md)，通过应用菜单的 **Topic/service/action peer communication demo** 启用。

Wi-Fi UDP、USB RNDIS 网络模式、WSL Agent 启动和网络收发测试见 [NETWORK.md](NETWORK.md)。

参考 mcu_test/esp32s3_test 的 ESP-IDF + Src 布局，接入官方 micro_ros_espidf_component Jazzy 分支。组件必要源文件已放在 components 下，23 个文件的 Git blob SHA 已验证，版本与来源记录在组件 UPSTREAM.json。

公共 micro-ROS 应用位于 ../firmware/app，与 STM32 编译同一份源码。本工程支持 USB Serial/JTAG、UART1、Wi-Fi UDP 和 USB RNDIS UDP；网络模式共用 firmware/transport/udp_transport.c，不使用参考工程的旧消息协议。USB RNDIS 使用本工程内的 TinyUSB 运行时源文件。

所有传输统一使用项目根目录的一个 `sdkconfig`，在 `idf.py -DMICROROS_PREPARE=ON menuconfig` 中选择；菜单定义位于 `Src/Kconfig.projbuild`。`sdkconfig.defaults` 仅提供初始默认值，不再使用 Wi-Fi/USB 各自的配置文件或默认值文件。旧本地配置保留在 `build/config_backup`，当前主配置选择 USB RNDIS。RNDIS 选择会自动联动 TinyUSB 网络驱动。

- 默认芯片原生 USB Serial/JTAG，GPIO19 D-、GPIO20 D+；连接开发板标为 USB/OTG 的原生 USB 插口，不是 UART 桥插口。
- 可在 menuconfig 的 ESP32-S3 ROS 2 application 中选择 UART1：默认 GPIO17 TX、GPIO18 RX，外接 USB-UART，115200 8N1。
- 16 MB Flash 默认配置来自参考板；烧录前按实际模组容量调整。示例不要求 PSRAM，默认关闭。
- micro-ROS 任务固定在 CPU0，栈大小 16 KiB（ESP-IDF 参数单位为字节）。
- 各模式默认 UART0 115200 诊断，原生 USB 或 UART1 数据口仅传 XRCE 帧；bootloader 日志关闭。旧 sdkconfig 保留已有控制台选择，可在 menuconfig 中修改。
- 节点 /esp32s3/esp32s3；heartbeat 每秒发布 Int32；command 原值回传 echo。
- Agent 未启动时持续等待；断线后销毁实体并重建。默认 ROS_DOMAIN_ID=0。
- app-colcon.meta 使用 custom transport，支持串口 framing 和 UDP 数据包模式；资源配置为 1 节点、10 发布者、5 订阅者、4 service、4 client、12 history slots，支持可选的两板通信示例。

修改节点/收发/重连逻辑请编辑 ../firmware/app/micro_ros.c；心跳与超时参数编辑 ../firmware/app/micro_ros_config.h。本工程 Src/micro_ros_platform.c 只提供 ESP-IDF 初始化、配置、时间和延时。

## Windows + WSL（与 STM32 相同的分工）

2026-10-08：单一配置结构验证通过；ESP-IDF 四种传输的配置生成、RNDIS 依赖联动检查通过。WSL 生成 74 个包、1,695 个对象的匹配库，Windows 完整构建 USB RNDIS 固件通过，bin 为 394,224 B，库指纹和分区检查通过。记录见本机 `build/single_config_validation.json`；本次未重新烧录。

Windows 使用已有 ESP-IDF 编译、烧录应用；WSL 生成 Xtensa micro-ROS 静态库并运行 Agent/ROS 2。WSL 复用 Windows ESP-IDF 的源码、头文件和配置，不克隆 Linux ESP-IDF，也不创建 Linux IDF Python 环境。首次生成库时，仅下载与 Windows GCC 完全相同发行版本的 Linux Xtensa 编译器，保存到 WSL 的 `~/.cache/mcu_ros2/espressif`。

在已激活的 **Windows ESP-IDF PowerShell** 中，进入本工程：

```powershell
cd C:\Users\admin\Desktop\mcu_ros2\esp32s3_ros2
$env:PYTHONUTF8 = "1"
idf.py -DMICROROS_PREPARE=ON reconfigure
```

这一步只配置工程，将库构建请求写入 `build/micro_ros_request.json`；尚不能构建固件。目标默认为 esp32s3，如需要切换目标使用 `idf.py -DMICROROS_PREPARE=ON set-target esp32s3`。

在 **WSL Ubuntu** 中生成库：

```bash
cd /mnt/c/Users/admin/Desktop/mcu_ros2/esp32s3_ros2
# 已按 STM32 流程安装过 WSL 工具时通常无需重复安装。
# 缺依赖时执行：
# sudo apt-get update
# sudo apt-get install -y build-essential git cmake python3-colcon-common-extensions \
#     python3-vcstool python3-catkin-pkg python3-lark python3-empy python3-numpy
bash scripts/build_micro_ros.sh
```

脚本使用官方 `libmicroros.mk` 和 Jazzy 源码，Linux 构建缓存放在 WSL 的 `~/.cache/mcu_ros2/esp32s3`，其中会缓存 Windows SDK 所需头文件和配置，避免跨系统读取拖慢编译。完成后输出到 `lib/micro_ros/jazzy`。`build_manifest.json` 记录配置指纹，`source_commits.json` 记录源码提交。自定义 Windows 构建目录时，将导出的请求路径作为参数，例如 `bash scripts/build_micro_ros.sh /mnt/c/.../build-custom/micro_ros_request.json`。

回到 **Windows ESP-IDF PowerShell**：

```powershell
idf.py -DMICROROS_PREPARE=OFF build
# 接入开发板后按实际端口烧录：
idf.py -p COM5 flash
```

VS Code 默认烧录方式为 UART/esptool，可通过 `ESP-IDF: Select Port to Use` 选择实际 USB 串口，再执行 `ESP-IDF: Flash (UART) your Project`。这个方式也支持芯片原生 USB Serial/JTAG 的 COM 口，不要求连接外部 USB-UART。[官方烧录说明](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/flashdevice.html)、[USB Serial/JTAG 下载](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html)。

如果开发板仍运行旧 RNDIS/TinyUSB 固件，Windows 可能只显示 USB 网络设备（参考固件为 VID:PID 303A:4008），此时 OpenOCD 无法找到原生 JTAG。保持原生 USB 插口连接，按住 BOOT，短按 RESET，然后松开 BOOT；在设备管理器找到新出现的 USB 串口，再按实际 COM 号执行上面的烧录命令。若设备已转接给 WSL，应先将这个设备从 WSL 分离，使 Windows 能访问它。[官方恢复下载模式说明](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html#usb-pin-reconfiguration)。

如需要 JTAG 调试，再安装 Espressif USB JTAG 驱动，并使用 `board/esp32s3-builtin.cfg`。EIM 的 `Install Drivers` 可以安装所需驱动。[官方 JTAG 驱动说明](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/jtag-debugging/configure-builtin-jtag.html)。

以后只修改 `firmware` 或 `Src` 中应用代码，直接 `idf.py build` 即可。更改 SDK、sdkconfig、app-colcon.meta 或额外 ROS 包时，重新执行上述“Windows 配置 → WSL 生成库 → Windows 构建”三步。调整配置可使用 `idf.py -DMICROROS_PREPARE=ON menuconfig`，保存后重新导出配置并生成库。配置阶段检测到库缺失或不匹配会明确报错，避免使用旧库。WSL 中运行 Agent 的命令见下文，与 STM32 共用 Tools。

## WSL Agent 与收发

原生 USB 接入 WSL 后通常为 /dev/ttyACM0，实际名称以 ls /dev/ttyACM* 为准。USB Serial/JTAG 与原生 TinyUSB OTG 共享 PHY，首版使用前者；旧 TinyUSB CDC/RNDIS 固件烧录后，USB 枚举和串口名可能变化。需要时按住 BOOT 再按 RESET 进入 ROM 下载。

烧录后，在 mcu_ros2 目录的新终端执行：

```bash
export ROS_DOMAIN_ID=0
bash Tools/scripts/agent.sh /dev/ttyACM0 jazzy
```

再开 ROS 2 终端：

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0
ros2 node list
ros2 topic echo /esp32s3/heartbeat
```

command/echo 测试见 [总说明](../README.md)。Agent 中出现 create_client 和实体创建后，应能看到节点；只有 running... 表示端口启动。

## 当前验证

最新 RNDIS + WSL 实板验证（2026-10-07 23:49）：MCU `192.168.7.1` 经 Windows RNDIS 网卡和 WSL mirrored 网络连接到 WSL 中的 `micro_ros_agent`，Agent 地址为 `192.168.7.2:8888`，ROS_DOMAIN_ID=0。Windows 与 Hyper-V 的 UDP 8888 入站允许规则已确认存在；Agent 日志出现 `create_client` 和 `session established`。WSL rclpy 测试收到 heartbeat `84、85、86`，发送 command=57007 后收到 13 次对应 echo，双向实板收发通过。验证记录为 `../Tools/build/wsl_agent/validation.json`，Agent 日志为 `../Tools/build/wsl_agent/agent.log`；这些本地测试产物不随 Git 提交。下面保留此前各阶段的验证记录。

RNDIS 实板与 Windows 原生 Agent 验证（2026-10-07）：主配置已切到 USB RNDIS，启用 TinyUSB ECM/RNDIS 类、UART0 日志和 USB DHCP；匹配库重新生成、固件构建及 COM13 烧录成功。Windows 枚举到 303A:4008 的 RNDIS 网卡并获得 192.168.7.2/24，MCU 为 192.168.7.1；WSL ping 三次通过。Windows Micro XRCE-DDS Agent 2.4.3 编译和本机 XRCE ping 通过，与实板建立会话、话题和读写端点。Windows 原生 DDS 测试两次收到三个递增 heartbeat、12 次 command=57007 的 echo 回包；测试实例均已停止，8888 已释放。WSL ROS 话题跨系统 DDS 接收仍未通过。记录见 `build/rndis_test_validation.json`；运行与测试命令见 [Tools/README.md](../Tools/README.md)。

实板 Wi-Fi 诊断（2026-10-07）：用户提供的 UART 日志确认最新 KK 固件已启动，芯片成功连接信道 12 并获得 `192.168.45.67`；WSL ping 该地址三次成功，Agent 本机 XRCE ping 三次成功。WSL 的 Hyper-V 入站策略为 Block，未见 UDP 8888 允许规则；12 秒抓包未见 MCU 的 UDP 8888 数据进入 WSL。当前会话没有 Windows 管理员权限，尚未添加防火墙规则，实板 ROS 话题收发仍待验证。规则命令见 [NETWORK.md](NETWORK.md)。诊断时的后台 Agent 已停止，以便用户从终端启动；历史日志为 `../Tools/build/wifi_kk_agent.log`。

Wi-Fi 配置更新（2026-10-07）：当前本地主 `sdkconfig` / `build` 使用 Wi-Fi UDP，SSID 为 `KK`，地区为 `CN`，Agent 为 `192.168.45.245:8888`；UART0 诊断日志为 115200。WSL 重新生成 74 个包、1,695 个对象的库，Windows 完整构建通过，固件为 850,128 B。COM13 被串口监视器占用，烧录尝试未能打开端口，实板连接及话题收发尚未测试。记录：`build/wifi_kk_validation.json`；烧录前需要退出监视器。

网络扩展验证（2026-10-07）：USB 串口、Wi-Fi UDP、USB RNDIS UDP 均完成 Windows + WSL 的库生成与完整固件构建；STM32 Debug/Release 通过。共享 UDP 回调与真实 Agent 的 XRCE ping、真实 XRCE Client 的三次会话建立/销毁通过，直接 Ninja 构建会拒绝不匹配的库。默认串口库已恢复。结果记录在 `build/network_validation.json`；本次未烧录，实板网络与 ROS 话题收发仍需按 [NETWORK.md](NETWORK.md) 测试。

2026-10-07：Windows + WSL 流程完整验证通过。复用本机 `C:/esp/v6.1/esp-idf` 和 esp-15.2.0_20251204 工具链；WSL 完成 74 个包，生成含 1,695 个对象的 Xtensa 静态库。Windows ESP-IDF 完成 1,082 个构建步骤，成功链接 `build/esp32s3_ros2.elf` 并生成 `build/esp32s3_ros2.bin`（238,048 B），分区大小检查通过。Windows/WSL 指纹一致，设置不匹配时拒绝链接的检查通过。过程未安装 Linux ESP-IDF。

验证记录：`build/workflow_validation.json`；Windows 构建日志：`build/windows_build.log`。烧录与实板收发尚未测试，需要接入开发板后验证。

官方组件 Jazzy 分支说明已测试 IDF 5.2、5.3、5.4、5.5、6.0；参考工程的 6.1 需要完整构建及实板验证。来源：[官方组件](https://github.com/micro-ROS/micro_ros_espidf_component/tree/jazzy)。

ESP32 专用库位于 lib/micro_ros/jazzy。工程使用 MICROROS_PREBUILT 模式，由 cmake/micro_ros_prebuilt.cmake 导出请求并链接 WSL 库；组件 CMake 有一个进入该模式的本地适配入口。
