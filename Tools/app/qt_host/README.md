# Qt ROS 2 工作台

无需 Qt 的部署请使用 [浏览器工作台](../web/README.md)：独立 HTTP / WebSocket 服务复用 ROS worker，浏览器或自定义前端负责显示和交互。本页仍描述原有 Qt 进程通信方式。

`qt_ros2` 是 Qt 6 Widgets / C++17 上位机，支持 Windows 原生界面和 Linux 本地运行。按 `mcu_test/Tools` 的 `core → runtime → app` 结构设计，整个 Tools 只有一份主 CMakeLists 和两个构建 preset，不拷贝参考工程的私有通信协议及第三方库。

## 平台与运行方式

| 平台 | 界面 | ROS 2 运行位置 | 已验证 |
| --- | --- | --- | --- |
| Windows | 原生 `qt_ros2.exe`，本机 Qt 6.11.1 MinGW | 默认 WSL 发行版里的 Jazzy / rclpy / Agent | 两块实板 Topic、Service、Action、传感器测试话题、重连 |
| Linux / WSL | 原生 `qt_ros2`，Qt 6.4 及以上 | 本地 Jazzy / rclpy / Agent | 编译、两块实板 Topic、Service、Action、重连 |

Windows 版目前仍需 WSL 中的 ROS 2 和 Agent。界面通过 `QProcess` 启动 WSL ROS 后端并交换 JSON 行消息，后端负责启动或复用 UDP Agent；用户可以直接从 Windows Qt 操作，无需另外打开 WSL 终端。界面保持 Windows 原生运行，不要求 Windows 安装 ROS 2。当前 Agent 进程运行在 WSL，尚未接入 Windows 原生 Agent 后端。

完全原生的 Windows ROS 2 是另一种部署方式，需要对应的 ROS 2 安装及兼容工具链，本程序尚未实现这个后端。Jazzy 的近期补丁发行说明已注明不再提供 Windows 二进制包，见 [ROS 2 官方发行说明](https://github.com/ros2/ros2/releases)。Qt 本身支持 Windows，见 [Qt 官方 Windows 文档](https://doc.qt.io/qt-6/windows.html)。

## 分层

```text
Tools/
├── CMakeLists.txt                 # Qt 主构建入口
├── CMakePresets.json              # default → build/windows；linux → build/linux
├── core/model/include,src        # BoardModel，标准 C++，不依赖 Qt 或 ROS
├── runtime/bridge/include,src    # RosSession：进程、JSON、启动/退出、异步事件
├── runtime/ros2                  # rclpy、ROS 图、AgentRuntime 的进程生命周期与 XRCE 检查
├── runtime/ui/include,src        # MainWindow：展示、输入与用户操作
└── app/qt_host
    ├── src/main.cpp             # 程序组合和命令行入口
    └── tests                    # 界面实板测试、后端协议测试、明确标注的传感器夹具
```

界面 → RosSession → JSON stdin/stdout → rclpy → DDS / Agent → MCU。板间 Topic、Service、Action 继续由 MCU 和 Agent 原生完成；Qt 后端没有板间 Topic 转发功能。

`RosSession` 启动时使用独立参数列表，不拼接用户输入成 shell 命令；命令异步发送，界面不会等 ROS 请求而阻塞。后端 stdin 线程仅入队，所有 ROS API 与回调在同一 executor 线程运行。

后端启动限时 20 秒，Service / Action 请求限时 12 秒；每块板允许一个上位机 Action 目标。断开/关闭取消本界面的目标并退出后端：停止本程序创建的 Agent，保留复用的外部 Agent。重连创建新 ROS 节点，清空旧板卡在线状态。日志最多 500 行，传感器预览最多 10 Hz，长数组只显示前 64 项；传感器非有限浮点显示为字符串。

Agent 启动/停止与 ROS 请求一样异步处理。通过 XRCE GET_INFO / INFO_ACTIVITY 实际响应确认 Agent 就绪，不把“端口被占用”当成成功；无响应时 5 秒内显示错误。已运行的 Agent 被标记为“复用外部”，停止按钮禁用。本程序创建的 Agent 可停止/重启，退出时最多等待 2 秒后清理。Agent 日志线程只入队，主循环输出事件，队列限制 100 行，界面统一显示。

## Windows 编译与运行

本机 preset 已配置 Qt 和 MinGW 路径。以下命令在 **Windows PowerShell** 中运行，编译 Qt 程序不需要 Linux ESP-IDF：

```powershell
cd C:\Users\admin\Desktop\mcu_ros2\Tools
$env:PATH = 'C:\Qt\Tools\CMake_64\bin;C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
cmake --preset default
cmake --build --preset default
ctest --preset default
windeployqt --release --no-translations --no-opengl-sw --compiler-runtime build/windows/qt_ros2.exe
.\build\windows\qt_ros2.exe --connect
```

部署 Qt DLL 后可以双击 `build/windows/qt_ros2.exe`。默认勾选“连接时启动 / 复用 Agent”，点击“连接 ROS 2”会同时管理后端和 Agent。复制程序时须保留同目录的 DLL、Qt 插件、`runtime/ros2` 和 `config`；目标电脑仍需 WSL / ROS 2，并且需要可发现的 micro_ros_agent 安装。程序目录须位于 WSL 默认 `/mnt/<盘符>/...` 可访问的 Windows 本地磁盘；网络共享路径和自定义 WSL automount 路径暂不支持。

ROS 2 发行版默认为 `jazzy`，WSL 发行版留空表示系统默认；存在多个 WSL 时填写 `wsl -l -q` 显示的名称。后端读取该 Linux 环境 `/opt/ros/<发行版>/setup.bash`，需要 rclpy、example_interfaces、sensor_msgs、rosidl_runtime_py；现有 Jazzy desktop 环境已具备这些依赖。

本机 Agent 已构建在 `Tools/build/micro_ros/jazzy/agent/install`，程序会自动加载 overlay；Linux 系统安装的 micro_ros_agent 也可从 ROS package index 发现。Agent 未构建时，先执行 `bash Tools/scripts/build_agent.sh jazzy`。Agent 默认 UDP 8888，需与 MCU 固件的地址/端口配置一致。

如果希望 Agent 在 Qt 断开后仍独立运行，可以先从**仓库根目录**手动启动，Qt 会检测并复用：

```bash
export ROS_DOMAIN_ID=0
export FASTRTPS_DEFAULT_PROFILES_FILE="$PWD/Tools/config/fastdds_wsl_local.xml"
bash Tools/scripts/agent.sh udp 8888 jazzy
```

Agent 已运行时直接复用，程序不会重复绑定 8888。界面默认勾选“DDS 仅本机”，与该 profile 一致：ROS 后端和 Agent 同处 WSL，XRCE UDP 仍接收两块 USB 网卡上的 MCU。取消勾选使用 Linux 环境的 DDS 配置；连接跨电脑 ROS 节点时，Agent、后端及网络配置都需要匹配。现有 Windows 原生 Agent 不属于这个 WSL loopback DDS 会话。

## Linux / WSL 编译

```bash
sudo apt install qt6-base-dev cmake ninja-build g++
cd /mnt/c/Users/admin/Desktop/mcu_ros2/Tools
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
./build/linux/qt_ros2 --connect
```

有 WSLg / Linux 桌面时可显示窗口；无显示环境可设置 `QT_QPA_PLATFORM=offscreen` 运行测试。两个平台使用不同的 build 目录，避免 Windows / WSL 混用 CMakeCache。

## 界面功能

* Agent：连接时默认自动启动/复用；可以手动“启动 / 检查 Agent”、查看运行归属、停止本程序 Agent 和修改 UDP 端口。停止/断开本程序创建的 Agent 后，MCU 会等待重新连接；复用的外部实例不受影响。
* 板卡表：`esp32s3`、`stm32` 的心跳、command 回显、peer_received、roundtrip、板间 Service / Action 诊断。连续 3.5 秒没有心跳显示“等待心跳”；ROS 后端就绪不等于板卡在线。
* Topic：发布 `/<board>/command` 的 Int32，在 `echo` 列观察 MCU 回传。
* Service：调用 `/<board>/add_two_ints`。输入与返回值用十进制字符串跨 JSON 传递，保留完整 Int64 精度；当前固件求和溢出按 Int64 上下界饱和。
* Action：调用 `/<board>/fibonacci`，显示目标接受、反馈、成功/拒绝/取消结果。当前固件序列长度为 2–10；6 返回 `[0,1,1,2,3,5]`，11 用于测试拒绝。
* 传感器：从 ROS 图列出 `sensor_msgs/msg/*` 话题，选择后以 sensor-data QoS 订阅并展示字段。新增传感器后在“节点与话题”页刷新。标准类型包括 Imu、Temperature、LaserScan、JointState、Image 等，不需要为这些标准接口自行定义消息；实际数据仍需固件中的传感器驱动和 publisher。当前两块板没有物理传感器发布。
* 节点与话题：手动刷新 ROS 节点、Topic 类型与 Service 列表。事件记录可清空或导出。

## 验证

`ctest` 是无硬件的命令行启动检查，不代表实板通信已验证。显式执行界面实板测试（已运行的 Agent、两块固件通信 demo 和 Domain 0 必须一致）：

```powershell
# Windows：通过实际 Qt 按钮驱动后端和真实 MCU
.\build\windows\qt_ros2.exe --smoke-test build/windows/hardware_report.json --screenshot build/windows/window.png
```

```bash
# Linux / WSL：没有显示环境时使用 offscreen
QT_QPA_PLATFORM=offscreen ./build/linux/qt_ros2 \
  --smoke-test build/linux/hardware_report.json --screenshot build/linux/window.png

# 后端真实 ROS 协议检查，不要求 Agent/MCU；仅发送无效 MCU 命令
source /opt/ros/jazzy/setup.bash
python3 app/qt_host/tests/test_worker.py
```

界面实板测试验证至少三个相同序号的原生板间回环、两块板各自 command/echo、超出 JSON 精确整数范围的 Int64 求和、Action 成功/拒绝/取消，随后断开重连并重新收到两块板心跳。后端检查验证坏 JSON 后继续工作、Int32/Int64 越界及无效请求拒绝、真实 sensor_msgs 订阅、NaN 序列化、stdin EOF 后进程退出。生成文件均放在忽略的 `Tools/build` 中。

需要验证传感器页面时，在另一个 WSL 终端从 Tools 目录启动明确标注的**主机生成测试话题**（60 秒后自行退出）：

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0
export FASTRTPS_DEFAULT_PROFILES_FILE="$PWD/config/fastdds_wsl_local.xml"
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
export ROS_STATIC_PEERS=127.0.0.1
python3 app/qt_host/tests/sensor_fixture.py
```

然后在上述 Qt 测试命令后添加 `--sensor-test`。它订阅 `/qt_ros2_test/temperature` 并检查界面显示 25.5；该数据来自电脑测试夹具，不代表 MCU 的物理传感器测量。

2026-10-08 本机验证：Windows / Linux 编译及 CTest 通过，两块实板界面通信与重连通过，Windows 传感器页面夹具检查通过，后端协议检查通过。实板端到端验证使用 Domain 0；其它 Domain 尚未完成端到端验证，需匹配 MCU / Agent 并检查 DDS 发现路径。

Agent 管理测试使用备用 UDP 18888，不影响已有 8888 实例；通过真实 Qt 按钮验证启动、XRCE 就绪、停止、重启和断开清理：

```powershell
.\build\windows\qt_ros2.exe --agent-test build/windows/agent_report.json
```

```bash
QT_QPA_PLATFORM=offscreen ./build/linux/qt_ros2 --agent-test build/linux/agent_report.json
# 已有 Agent 8888 的复用/禁止误停，以及非 XRCE UDP 服务不会被误认成 Agent
python3 app/qt_host/tests/test_agent_reuse.py --existing-port 8888
```
