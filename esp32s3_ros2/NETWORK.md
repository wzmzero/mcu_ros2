# micro-ROS 网络传输

本工程所有传输都使用同一个 `sdkconfig` 和默认 `build` 目录。通过 menuconfig → ESP32-S3 ROS 2 application → micro-ROS transport 选择 USB Serial/JTAG、UART1、Wi-Fi UDP 或 USB RNDIS UDP。`sdkconfig.defaults` 只是首次创建配置时的默认值，不是第二套运行配置；`sdkconfig.old` 是 ESP-IDF 自动备份。

不再使用按传输划分的配置和构建目录。旧本地配置已移到 `build/config_backup`，保留原有设置；当前主 `sdkconfig` 选择 USB RNDIS。菜单定义位于 `Src/Kconfig.projbuild`，STM32 也采用这个位置，并在自己的单一 `sdkconfig` 中选择 UART / USB RNDIS。

## 配置与构建

在已激活的 Windows ESP-IDF PowerShell 中进入 esp32s3_ros2：

```powershell
$env:PYTHONUTF8 = "1"
idf.py -DMICROROS_PREPARE=ON menuconfig
# 选择传输方式，并填写 Agent 地址、端口、ROS 域等参数。
idf.py -DMICROROS_PREPARE=ON reconfigure
```

在同一个工程的 WSL 终端生成匹配的 micro-ROS 静态库：

```bash
bash scripts/build_micro_ros.sh
```

回到 Windows 构建和烧录：

```powershell
idf.py -DMICROROS_PREPARE=OFF build
idf.py -p COM13 flash
```

COM13 按实际串口替换，先退出占用该端口的监视器。只修改应用代码时直接 `idf.py build`。修改 sdkconfig、SDK 或 micro-ROS 参数后，需要重新执行“配置 → WSL 生成库 → Windows 构建”；每次构建都会检查库指纹，防止使用不匹配的库。以上流程复用已有 Windows ESP-IDF，不需要安装 Linux ESP-IDF。

所有模式默认使用 UART0 115200 输出诊断，USB 原生接口或 UART1 传输 XRCE 数据。旧 sdkconfig 会保留之前的控制台选择；如需要日志，在 Component config → ESP System Settings → Channel for console output 选择 UART0，避免把诊断日志写入传输 XRCE 的原生 USB。

## Wi-Fi UDP

菜单选择 Wi-Fi + lwIP UDP，配置 SSID、密码、country code、Agent IPv4 和 UDP 端口。SSID/密码仅保存在本机 sdkconfig，请勿提交该文件；空 SSID 无法联网。Wi-Fi 使用 STA + DHCP，掉线后自动重连，关闭省电减少等待。中国部署设置 country code 为 `CN`，以支持接入点的信道 12/13。

Agent 地址填写 MCU 能访问的电脑局域网地址。更换热点或 DHCP 地址后需要更新；从 UART0 日志的 `Wi-Fi ready: IP=...` 确认 MCU 地址。不要用 ping 电脑自己的地址判断 MCU 是否在线。

WSL 默认 NAT 的私有地址通常不能直接被 Wi-Fi MCU 访问。本机使用 mirrored 网络，Windows 和 Hyper-V 防火墙需要放行 MCU 发往 UDP 8888 的入站数据。在仓库根目录的管理员 PowerShell 中，例如 MCU 为 192.168.45.67：

```powershell
.\Tools\scripts\configure_agent_firewall.ps1 -Target wsl -McuAddress 192.168.45.67 -Port 8888
```

已有规则会更新地址；DHCP 地址变化后也需要更新。netsh portproxy 的 TCP 转发不能代替 UDP 转发。

## USB RNDIS UDP

菜单选择 USB OTG RNDIS network + lwIP UDP。选择该模式后，TinyUSB 自动使用 ECM/RNDIS 并关闭竞争的 CDC/NCM，无需额外配置文件。检查 Agent 地址为电脑 USB 网卡的地址，默认为 `192.168.7.2:8888`；已有 sdkconfig 可能保留上次填写的 Wi-Fi Agent 地址，需要在菜单中调整。

连接芯片原生 USB/OTG 接口（GPIO19 D-、GPIO20 D+），不是 USB-UART 插口。RNDIS 占用 USB OTG PHY，运行时原生 USB Serial/JTAG 不可用。需要从原生 USB 下载时，按住 BOOT、短按 RESET、松开 BOOT，进入 ROM 下载并选择实际 COM 端口。也可通过开发板的 UART 桥烧录。

固件枚举为 RNDIS 设备（303A:4008）。MCU 固定为 192.168.7.1/24，默认启用 DHCP，为 USB 主机提供 192.168.7.2–3；不发布默认网关和 DNS，不需要路由器。STM32 使用 MCU 192.168.7.3、主机及 Agent 192.168.7.4。两块板同时使用时，ESP32 的电脑网卡固定为 .2（避免 DHCP 分配 .3），并为两个 MCU 添加各自 USB 网卡的 /32 路由，详见 [STM32 网络配置](../stm32f4_ros2/README.md#usb-rndis-模式)。

本机优先让 Windows 接管 RNDIS 网卡，WSL mirrored 网络复用该网络，不执行 usbipd attach。先确认 Windows 网卡是否自动获得 192.168.7.2/24：

```powershell
Get-NetAdapter | Format-Table ifIndex,Name,InterfaceDescription,Status
ping 192.168.7.1
```

仅在未使用 DHCP 时，为实际 RNDIS 网卡手动设置 192.168.7.2/24，不设置默认网关。两块板同时使用时，管理员 PowerShell 放行两个 MCU 地址：

```powershell
.\Tools\scripts\configure_agent_firewall.ps1 -Target wsl -McuAddress 192.168.7.1,192.168.7.3 -Port 8888
```

Windows 原生 Agent 也可以直接使用该网卡，见 [Tools/README.md](../Tools/README.md)。

若另一台机器的 WSL 内核支持 RNDIS，并选择 USB 直通给 Linux，可用 usbipd bind/attach 转接该设备，再用 `ip -br link` 找到实际网卡并设置 192.168.7.2/24。本机 WSL 内核未启用 RNDIS host 驱动，因此使用 Windows 网卡路径。

## Agent 与 ROS 2 收发

在仓库根目录的 WSL 终端：

```bash
export ROS_DOMAIN_ID=0
bash Tools/scripts/agent.sh udp 8888 jazzy
```

已有 UDP 8888 Agent 时直接复用，一个 Agent 可接收两块板，不要重复绑定同一端口，也不要在测试时 Ctrl+C 停止 Agent。`running... port: 8888` 只说明监听成功；出现 create_client 和实体创建才说明 MCU 接入。

另开终端：

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0
ros2 node list
ros2 topic echo /esp32s3/heartbeat
# 另开终端监听 echo，再从一个终端发送：
ros2 topic echo /esp32s3/echo
ros2 topic pub --once /esp32s3/command std_msgs/msg/Int32 '{data: 123}'
```

应看到节点 /esp32s3/esp32s3、递增 heartbeat 和 echo=123。固件和所有 ROS 终端使用同一个域。网络传输发送完整 XRCE UDP 数据包，关闭串口 framing，复用共享应用的断线重连；无需自定义 Agent。

软件测试使用 `bash Tools/scripts/test_agent.sh jazzy`，不能代替实板话题收发。此前 ESP32 USB RNDIS 的 Windows/WSL heartbeat、command/echo 已通过，记录保留在 Tools/build 与本工程 build 中。TinyUSB/esp_tinyusb 的来源及本地修改见各组件 UPSTREAM.json。

官方资料：[micro-ROS 自定义传输](https://github.com/micro-ROS/micro-ros.github.io/blob/master/_docs/tutorials/advanced/create_custom_transports/index.md)、[ESP-IDF lwIP](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/lwip.html)、[WSL mirrored 网络](https://learn.microsoft.com/en-us/windows/wsl/networking#mirrored-mode-networking)。
