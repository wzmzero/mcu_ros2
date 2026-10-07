# micro-ROS 网络传输

ESP32 可在 menuconfig → ESP32-S3 ROS 2 application → micro-ROS transport 选择原生 USB Serial/JTAG、UART1、Wi-Fi UDP 或 USB RNDIS UDP。默认仍是 USB Serial/JTAG。STM32 继续使用 UART，不需要网络硬件。

网络模式通过 lwIP Socket 发送完整 XRCE UDP 数据包，公共应用按平台配置关闭串口 framing。Agent 使用标准 udp4，无需自定义 Agent。heartbeat、command、echo、域 ID 和断线重连逻辑共用。

## Wi-Fi

在已激活的 Windows ESP-IDF PowerShell，从 esp32s3_ros2 目录执行：

```powershell
$env:PYTHONUTF8 = "1"
idf.py -B build-wifi "-DSDKCONFIG=sdkconfig.wifi" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.wifi.defaults" -DMICROROS_PREPARE=ON menuconfig
# 设置 Wi-Fi SSID、密码、MCU 可以访问的 Agent IPv4 和 UDP 端口。
idf.py -B build-wifi -DMICROROS_PREPARE=ON reconfigure
```

SSID 与密码保存在本地 sdkconfig.wifi，请勿提交或分享该文件；空 SSID 无法联网。Wi-Fi 使用 STA + DHCP，掉线后自动重连，关闭省电减少传输等待。

Wi-Fi 配置默认启用 UART0 日志（115200），可通过开发板的 USB-UART 插口查看连接、DHCP 地址及断开原因；日志不打印密码。原生 USB 插口与 UART0 不是同一个控制台。已有 `sdkconfig` 或 `sdkconfig.wifi` 不会自动采用新的默认值，需要在 `Component config → ESP System Settings → Channel for console output` 选择 UART0。串口传输模式仍需关闭控制台，避免日志混入 XRCE 数据。

`Wi-Fi country code` 按实际部署地区配置（中国为 `CN`），尤其在接入点使用信道 12/13 时。默认 `01` 为 world safe mode。Agent IPv4 填写电脑连接同一个 Wi-Fi 后的实际地址；更换热点或 DHCP 地址后需要更新。配置更改后必须重新生成 micro-ROS 库并构建、烧录，仅保存 menuconfig 不会改变板上固件。

如果使用 VS Code 默认的 `build` 目录，则在主 `sdkconfig` 中选择 Wi-Fi，并把下文的 `-B build-wifi` 去掉，WSL 请求文件改为 `build/micro_ros_request.json`。不要混用两个目录的配置和固件。串口监视器占用 COM 口时，先按 `Ctrl+]` 退出，再执行烧录。

WSL Ubuntu，从 esp32s3_ros2 目录生成库：

```bash
bash scripts/build_micro_ros.sh /mnt/c/Users/admin/Desktop/mcu_ros2/esp32s3_ros2/build-wifi/micro_ros_request.json
```

回到 Windows：

```powershell
idf.py -B build-wifi -DMICROROS_PREPARE=OFF build
idf.py -B build-wifi -p COM6 flash
```

COM6 以实际端口为准，烧录时 USB 设备必须由 Windows 持有。

WSL 使用镜像网络时，Wi-Fi 已连接并不表示 UDP Agent 可从局域网访问。按 UART 日志中的 `Wi-Fi ready: IP=...` 确认 MCU 地址，并在管理员 PowerShell 放行该地址发往 UDP 8888 的流量。例如本机 KK 测试中 MCU 地址为 `192.168.45.67`：

```powershell
New-NetFirewallRule -Name MicroRosWifiUdp8888 -DisplayName "micro-ROS Wi-Fi UDP 8888" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 8888 -RemoteAddress 192.168.45.67
New-NetFirewallHyperVRule -Name MicroRosWifiUdp8888 -DisplayName "micro-ROS Wi-Fi UDP 8888" -Direction Inbound -Action Allow -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -Protocol UDP -LocalPorts 8888 -RemoteAddresses 192.168.45.67
```

已有规则时更新其远端地址，不重复创建；DHCP 地址变化后也需要更新。不要用 WSL ping 电脑自己的 Wi-Fi 地址来判断 MCU 通信，应 ping MCU 地址，并保持 Agent 运行；`Ctrl+C` 会停止 Agent。放行后检查 Agent 的会话创建日志及 `/esp32s3/heartbeat`、`/esp32s3/command`、`/esp32s3/echo` 话题。镜像网络的 Hyper-V 入站规则见 [微软说明](https://learn.microsoft.com/en-us/windows/wsl/networking#mirrored-mode-networking)。

## USB RNDIS 网络

使用芯片原生 USB/OTG 接口（GPIO19 D-，GPIO20 D+），不是外部 USB-UART 接口。RNDIS 使用 USB OTG + TinyUSB，占用内部 PHY；运行时原生 USB Serial/JTAG 不可用。需要烧录时先从 WSL detach，再按住 BOOT、短按 RESET、松开 BOOT，进入 ROM 下载后选择实际 COM 端口。

Windows ESP-IDF PowerShell：

```powershell
$env:PYTHONUTF8 = "1"
idf.py -B build-usb-network "-DSDKCONFIG=sdkconfig.usb-network" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.usb-network.defaults" -DMICROROS_PREPARE=ON reconfigure
```

WSL：

```bash
bash scripts/build_micro_ros.sh /mnt/c/Users/admin/Desktop/mcu_ros2/esp32s3_ros2/build-usb-network/micro_ros_request.json
```

Windows：

```powershell
idf.py -B build-usb-network -DMICROROS_PREPARE=OFF build
idf.py -B build-usb-network -p COM6 flash
```

固件枚举为 RNDIS 网络设备（303A:4008），UART0 保留诊断日志，原生 USB 用于网络。MCU 固定为 192.168.7.1/24，默认启用 `APP_ROS_USB_DHCP_SERVER`，提供 192.168.7.2-3 地址池；单个 Windows USB 网卡通常自动取得 192.168.7.2，默认 Agent 为 192.168.7.2:8888。不分配默认网关和 DNS，不需要路由器或互联网。使用旧 `sdkconfig` 时需要明确启用 DHCP 和 UART0 控制台。

Windows 原生 Agent 可以直接使用这个网卡，命令与构建方式见 [Tools/README.md](../Tools/README.md)。这时 XRCE 数据链路不经过 WSL 的 Hyper-V 防火墙，Windows 自身的入站规则仍然适用。

### 本机推荐：Windows 网卡 + WSL 镜像网络

本机 WSL 已确认运行在 mirrored 模式，内核 6.6.87.2 的配置中 CONFIG_USB_NET_RNDIS_HOST 未启用。因此本机优先让 Windows 接管 RNDIS 网卡，不执行 usbipd attach。无需更换 WSL 内核。

先查看新 RNDIS 网卡是否已经从 DHCP 获得 192.168.7.2/24；已获得时直接 ping 192.168.7.1。只有禁用了固件 DHCP 或主机使用静态配置时，才在管理员 PowerShell 手动配置。按 InterfaceDescription 找到新增的 RNDIS 网卡，记录其 ifIndex。只给这张 USB 网卡设置地址，不设置默认网关：

```powershell
Get-NetAdapter | Format-Table ifIndex,Name,InterfaceDescription,Status
$usbIf = <实际RNDIS网卡的ifIndex>
New-NetIPAddress -InterfaceIndex $usbIf -IPAddress 192.168.7.2 -PrefixLength 24
ping 192.168.7.1
```

如果网卡已有正确的 192.168.7.2/24 地址，无需重复设置。若该子网已用于其他网络，请先换用不冲突的地址段并同步修改固件地址。

WSL 中用 ip -br address 查看接口，确认镜像网络可用；运行下面的 UDP Agent。若未同步新 USB 网卡，保存 WSL 工作后重启 WSL，再运行 Agent。按需添加限定到 MCU 地址和 UDP 8888 的入站规则；管理员 PowerShell：

```powershell
# 从 mcu_ros2 工程根目录执行；已有规则会更新，可重复运行。
.\Tools\scripts\configure_agent_firewall.ps1 -Target wsl -McuAddress 192.168.7.1 -Port 8888
```

Wi-Fi 测试可将 `-McuAddress` 改为实际 MCU 地址。ping 是主机主动发起的 ICMP，不能证明 MCU 主动发往 Agent 的 UDP 能通过 Windows/Hyper-V 入站检查。Windows 原生 Agent 的程序规则也不能授权 WSL Agent。保持 Agent 运行，放行后检查 `create_client` 和 heartbeat。2026-10-07 23:49 已确认 Windows/Hyper-V 的 UDP 8888 允许规则存在，并完成 WSL Agent 与实板的 heartbeat 和 command/echo 收发验证；记录见 `../Tools/build/wsl_agent/validation.json`。

### 其他环境：USB 网卡直接交给 WSL

仅在 WSL 内核具备 RNDIS 驱动时使用。管理员 PowerShell 使用 usbipd list 查找当前 BUSID，再执行 bind 和 attach（首次 bind 需要管理员权限）：

```powershell
$usbipd="C:\Program Files\usbipd-win\usbipd.exe"
& $usbipd list
& $usbipd bind --busid <实际BUSID>
& $usbipd attach --wsl --busid <实际BUSID>
```

WSL 必须具备 USB RNDIS 网络驱动。先用 ip -br link 找到新增的 usb0/enx... 网卡；以下将 usb0 替换成实际名称：

```bash
sudo ip link set dev usb0 up
sudo ip address replace 192.168.7.2/24 dev usb0
ip route get 192.168.7.1
ping -c 3 192.168.7.1
```

若没有新增网卡，检查 dmesg 中的 USB 枚举/驱动信息；仅 usbipd attach 成功不代表网络驱动已可用。不要给直连网卡设置默认网关。

## Agent 和 ROS 2 测试

从 mcu_ros2 根目录在 WSL 执行：

```bash
# 首次安装 Agent 时执行；已有可用 Agent 无需重建。
bash Tools/scripts/build_agent.sh jazzy
export ROS_DOMAIN_ID=0
bash Tools/scripts/agent.sh udp 8888 jazzy
```

在另一个 WSL 终端：

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0
ros2 node list
ros2 topic echo /esp32s3/heartbeat
```

再开终端监听 /esp32s3/echo，并发送：

```bash
ros2 topic pub --once /esp32s3/command std_msgs/msg/Int32 '{data: 123}'
```

应出现 /esp32s3/esp32s3 节点、递增 heartbeat，以及 echo 的 123。Agent 仅显示 running... 表示监听已启动；需要看到 create_client 和实体创建，才能说明 MCU 接入。

Wi-Fi 下 MCU 通常不能直接访问 WSL 默认 NAT 的私有地址。支持的 Windows 11/WSL 可以配置镜像网络，并允许 Windows/Hyper-V 防火墙的 UDP 8888 入站；Agent 地址填写 MCU 可达的主机局域网地址。netsh portproxy 的 TCP 转发不能代替 UDP 转发。USB 直通到 WSL 的网卡可使用上面的直连地址。

## 构建与验证边界

三个构建目录使用独立 sdkconfig，但共用 lib/micro_ros/jazzy 中的一份库。切换模式或修改 sdkconfig 后，要根据目标构建目录的 request 重新生成库；配置阶段及每次构建都会检查指纹，拒绝误用另一模式的库。回到默认串口模式：

```powershell
idf.py -B build -DMICROROS_PREPARE=ON reconfigure
```

```bash
bash scripts/build_micro_ros.sh
```

```powershell
idf.py -B build -DMICROROS_PREPARE=OFF build
```

软件测试：bash Tools/scripts/test_agent.sh jazzy。测试包含 UDP 回调的数据报边界、超时、来源过滤、超长数据包丢弃、关闭/重新打开，以及通过同一 UDP 回调与真实 Agent 进行 XRCE ping。它不代替 Wi-Fi 和 USB 实板的 heartbeat/command/echo 测试。

USB 网络依赖从本机 mcu_test 参考工程复制的 TinyUSB 运行时子集，包含该参考工程已有的 IDF 6.x 兼容修改；LICENSE、版本描述和各文件 SHA-256 记录在对应组件的 UPSTREAM.json，工程构建不依赖参考目录。

官方资料：[micro-ROS 自定义传输](https://github.com/micro-ROS/micro-ros.github.io/blob/master/_docs/tutorials/advanced/create_custom_transports/index.md)、[ESP-IDF lwIP](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/lwip.html)、[WSL 网络](https://learn.microsoft.com/en-us/windows/wsl/networking)。

2026-10-07 验证记录：三个 ESP32 模式完整构建通过；STM32 Debug/Release 通过；UDP 回调和真实 Agent ping、真实 XRCE Client 的三次会话创建/销毁通过；切换模式后的库不匹配防护通过。完整记录与产物校验值见 build/network_validation.json。本次未修改系统网卡地址/防火墙，也未烧录或完成实板话题收发。
