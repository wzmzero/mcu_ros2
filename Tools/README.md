# 上位机与测试工具

与 mcu_test 的 Tools 目录职责一致，这里存放 micro-ROS Agent 和通信测试工具。以下命令均从 mcu_ros2 根目录执行。

## Windows UDP Agent

本机可原生编译并运行 eProsima Micro XRCE-DDS Agent 2.4.3（UDP/TCP），复用已安装的 Visual Studio 2022 C++ 工具和 CMake，不依赖 Windows ROS 2 安装：

```powershell
.\Tools\scripts\build_agent_windows.ps1
.\Tools\scripts\agent.ps1 udp 8888
```

实板数据测试可直接在 Windows PowerShell 执行：

```powershell
.\Tools\scripts\test_agent_windows.ps1
```

测试订阅 `/esp32s3/heartbeat` 和 `/esp32s3/echo`，发送 `std_msgs/msg/Int32` 的 `command=57007`；收到至少三个不同 heartbeat 和三次对应 echo 后通过。测试通过 Fast DDS 使用 ROS 的主题名、类型名和 XCDR1 数据格式，不需要安装 Windows ROS 2。没有 Agent 时，测试会启动自己的隐藏实例并在结束后停止；已运行本项目 Windows Agent 时复用并保留它。`-Domain` 默认 0，`-Port` 默认 8888。当前 MCU 应用的 command 仅做 echo；更改应用的命令含义后应重新审查测试值。

程序及依赖 DLL 位于 `Tools/build/windows_agent/install/bin`。构建脚本处理旧 foonathan_memory 配置探针对新版 MSVC 的兼容问题，实际内存布局仍由编译器测量。Windows Agent 用于 Wi-Fi、RNDIS 等 IP 网络；此版本的 Windows 原生串口传输没有实现，现有串口测试仍使用 WSL Agent。基础 Agent 不包含 `micro_ros_agent` 的 ROS graph manager 扩展，因此不能用完整 ROS 节点图作为唯一验证，应检查实际话题数据。来源：[官方 Windows 安装步骤](https://micro-xrce-dds.docs.eprosima.com/en/v2.4.3/installation.html)、[Windows 传输实现](https://github.com/eProsima/Micro-XRCE-DDS-Agent/blob/v2.4.3/CMakeLists.txt)。

每个地址/端口运行一个 Agent，停止已有实例后再启动另一个。Windows 原生 Agent 仍受 Windows 防火墙影响。如果 Windows 自动生成针对 MicroXRCEAgent.exe 的 Public UDP 阻止规则，单独增加允许规则不能覆盖明确的阻止规则；可在 Windows 防火墙提示中允许相应网络，或从工程根目录在管理员 PowerShell 执行：

```powershell
.\Tools\scripts\configure_agent_firewall.ps1
```

此脚本只禁用当前 Agent 可执行文件的入站 UDP 阻止规则，并添加限定到 MCU `192.168.7.1`、UDP 8888、同一个 Agent 程序的允许规则。规则可重复更新；Wi-Fi 测试可通过 `-McuAddress <实际MCU地址>` 调整来源。USB 固件默认以 DHCP 分配电脑地址，详见 [NETWORK.md](../esp32s3_ros2/NETWORK.md)。

2026-10-07 实板验证：Windows 原生 Agent 构建通过，XRCE 本机 ping 三次通过；ESP32 RNDIS 成功枚举，Windows/WSL 获得 192.168.7.2，ping MCU 192.168.7.1 三次通过。Windows Agent 与实板会话及三个话题建立成功；原生 DDS 测试收到三个递增 heartbeat 和 12 次 echo，测试脚本重复验证通过。此前 Windows Public UDP 阻止规则已在现场改为 Allow；本工具没有执行管理员防火墙脚本。WSL 的 ROS 话题测试仍未收到数据，其跨系统 DDS 路径没有在本次打通；Windows 本机的数据链路已通过。记录见 `build/windows_agent/validation.json` 和 `../esp32s3_ros2/build/rndis_test_validation.json`。

## WSL Agent

USB RNDIS 在本机通过 Windows 网卡和 WSL mirrored 网络通信。`ping 192.168.7.1` 成功只能确认 ICMP；Agent 的 `running... port: 8888` 只表示监听成功。若没有 `create_client`，先从工程根目录在**管理员 PowerShell**执行：

```powershell
.\Tools\scripts\configure_agent_firewall.ps1 -Target wsl -McuAddress 192.168.7.1 -Port 8888
```

这会在 Windows 和 Hyper-V 防火墙中创建或更新仅允许该 MCU 发往 UDP 8888 的入站规则。默认 `-Target windows` 仍配置 Windows 原生 Agent。当前 WSL Agent 可保持运行，固件会自动重试；随后在另一个 WSL 终端订阅 `/esp32s3/heartbeat`。此选项适用于 mirrored 网络，不能替代 NAT 下的 UDP 转发。2026-10-07 后续排查确认：UDP 8888 允许规则已存在，WSL 中的 micro_ros_agent 与 MCU 建立会话。WSL rclpy 实板测试收到 heartbeat `84、85、86`，发送 command=57007 后收到 13 次对应 echo，双向收发通过。记录见 `build/wsl_agent/validation.json` 与 `build/wsl_agent/agent.log`，构建及测试产物均不随 Git 提交。

```bash
bash Tools/scripts/setup_wsl.sh jazzy
bash Tools/scripts/build_agent.sh jazzy
bash Tools/scripts/test_agent.sh jazzy
bash Tools/scripts/agent.sh /dev/ttyUSB0 jazzy  # STM32 USB-UART
bash Tools/scripts/agent.sh /dev/ttyACM0 jazzy  # ESP32 原生 USB
bash Tools/scripts/agent.sh udp 8888 jazzy     # Wi-Fi/以太网/USB 网络
```

设备需先接入 WSL，端口按实际枚举名称填写。两块板同时通信时，各自启动一个串口 Agent。

test_agent.sh 检查动态库、UDP XRCE ping、固件共用 UDP 回调（边界、超时、来源过滤、截断和重连）及 PTY 串口启动；这些属于软件测试。实板通信还需订阅 heartbeat，并发送 command 后检查 echo。ESP32 网络配置与烧录步骤见 [NETWORK.md](../esp32s3_ros2/NETWORK.md)。

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0
ros2 topic echo /stm32/heartbeat
# 另一个终端监听：ros2 topic echo /stm32/echo
ros2 topic pub --once /stm32/command std_msgs/msg/Int32 '{data: 123}'
```

ESP32 把话题中的 stm32 改成 esp32s3。

新的 Agent 工作区在 Tools/build/micro_ros；已有的旧工作区包含绝对路径，脚本会自动复用工程根目录或上一层的 build/micro_ros。可用 MICRO_ROS_HOST_ROOT 指定缓存根目录，该目录应包含 build/micro_ros。
