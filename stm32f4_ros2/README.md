# stm32f4_ros2

STM32F407VET6 + FreeRTOS + Jazzy micro-ROS。公共应用位于 `../firmware/app`，本工程 `Src` 提供硬件和传输适配。可以在编译前选择 USART1 DMA 或原生 USB RNDIS + UDP。

## 构建与 menuconfig

Windows 安装 STM32CubeCLT，将其 CMake、Ninja、GNU-tools-for-STM32 的 `bin` 加入 PATH；需要 Python 3 和 pip。WSL 使用 Linux 的 CMake、Ninja、ARM GCC 和 Python，不使用 Windows 的构建缓存。在 Windows PowerShell、CLion 或 WSL 终端中进入本目录，均使用下面的命令：

```powershell
cmake --preset Release
cmake --build --preset Release --target menuconfig
cmake --build --preset Release
cmake --build --preset Release --target flash_swd
```

菜单采用 [Kconfiglib](https://github.com/ulfalizer/Kconfiglib)，操作与 ESP-IDF menuconfig 类似：方向键移动，Enter 进入/编辑，Space 选择，S 保存，Q 退出。可配置传输方式、ROS 域、串口波特率；选择 USB RNDIS 后还可配置晶振频率、MCU/主机地址及 Agent 地址、端口。首次运行自动把 Kconfiglib 和 Windows 菜单所需的 windows-curses 安装到本工程 `.tools/menuconfig`，不需要 Linux 环境。

配置保存到 `sdkconfig`，构建目录生成 `generated/sdkconfig.h` 和 CMake 参数。保存菜单后直接 build，会自动重新配置并编译对应传输，不需要清空 build。`board.h` 引入生成的头文件，应用可以使用 `CONFIG_*` 宏。

只提供 `Debug`、`Release` 两个预设，表示优化和调试级别；两者都读取同一个 `sdkconfig`，第一次默认为 UART。UART / USB、ROS 域和网络参数统一在菜单中选择，不通过预设或 CMake `-D` 参数配置。菜单定义放在 `Src/Kconfig.projbuild`，与 ESP32 的应用配置位置一致。配置文件是本机配置，不提交 Git。配置生成、FreeRTOS、micro-ROS 与网络源码选择统一位于 `cmake/micro_ros.cmake`。

本工程顶层 `Makefile` 已移除。固件使用 CMake + Ninja，静态库生成使用 `scripts/build_micro_ros.sh`；该脚本直接提供编译参数给 micro-ROS 工具，不依赖顶层 Makefile。第三方工具中保留的官方 Makefile 教程属于其原始用法，本工程按本说明构建。

构建缓存自动按宿主系统隔离：Windows 放在 `build/Windows/<preset>`，WSL 放在 `build/Linux/<preset>`。因此在两边运行相同的 `cmake --preset Release` 不会再混用 `C:/...` 与 `/mnt/c/...` 路径。原来的 `build/Release` 和 `build/USB-*` 不再使用，不需要删除或修改旧缓存。

也可在第一次 CMake 配置之前直接打开菜单：Windows 执行 `python scripts/menuconfig.py --menuconfig`，WSL 执行 `python3 scripts/menuconfig.py --menuconfig`。

不通过菜单修改参数的示例：

```powershell
python scripts/menuconfig.py --config sdkconfig --set ROS_DOMAIN_ID=1
cmake --build --preset Release
```

产物为 `build/<host>/<preset>/stm32f4_ros2.elf`、`.bin`、`.hex`，链接地址 `0x08000000`。`flash_swd` 在找到当前系统可执行的 STM32CubeProgrammer CLI 后可用，需要 ST-Link；使用 Windows CubeCLT 时在 Windows 终端烧录。

## USB RNDIS 模式

在 menuconfig 的 `micro-ROS transport` 中选择 `Native USB RNDIS + UDP`，保存后执行 `cmake --build --preset Release` 即可构建 USB 固件。调试时使用 `Debug` 预设，同样读取该选择。

连接 MCU 原生 USB：PA11 为 D-，PA12 为 D+；CH340 和 ST-Link 接口不能提供 RNDIS。默认外部晶振 8 MHz，沿用 `C:\Users\admin\Desktop\mcu_test` 的板级参考。若实板晶振不同，先在菜单中修改。USB 模式通过 PLL 配置 CPU 168 MHz、USB 48 MHz。

| 参数 | 默认值 |
| --- | --- |
| MCU 地址 | 192.168.7.3/24 |
| Windows USB 网卡地址，板端 DHCP 分配 | 192.168.7.4/24 |
| Agent 地址、端口 | 192.168.7.4:8888 |
| ROS_DOMAIN_ID | 0 |

ESP32 使用 MCU 192.168.7.1、电脑 USB 网卡 192.168.7.2；STM32 使用 MCU 192.168.7.3、电脑 USB 网卡 192.168.7.4。电脑有两张独立 USB 网卡，因此 STM32 的 Agent 地址填写 .4，ESP32 填写 .2；同一个监听 0.0.0.0:8888 的 Agent 可以接收两块板的数据。STM32 DHCP 不发布默认网关和 DNS，其 USB 网卡保持自动获取地址即可。已有 sdkconfig 不会被菜单默认值覆盖，需要在 menuconfig 修改 MCU、主机 DHCP、Agent 三个地址。

两块板同时使用时，ESP32 的电脑 USB 网卡应固定为 192.168.7.2/24、不设置网关或 DNS。ESP-IDF 当前的 DHCP 地址池要求至少两个地址，ESP32 固件仍提供 .2–.3；电脑端使用静态 .2，避免 DHCP 分配到 STM32 的 .3。STM32 的电脑网卡通过 DHCP 获取 .4。

两张 USB 网卡同属 /24，仅区分 IP 还不能保证出站网卡正确。接上两块板并确认电脑具有 .2、.4 后，在**管理员 PowerShell**添加每块 MCU 的 /32 直连路由（命令可重复执行）：

```powershell
$links = @(
    @{ HostIp = '192.168.7.2'; McuIp = '192.168.7.1' },
    @{ HostIp = '192.168.7.4'; McuIp = '192.168.7.3' }
)
foreach ($link in $links) {
    $nic = @(Get-NetIPAddress -AddressFamily IPv4 -IPAddress $link.HostIp -ErrorAction SilentlyContinue)
    if ($nic.Count -ne 1) { throw "USB NIC missing or duplicate: $($link.HostIp)" }
    $prefix = "$($link.McuIp)/32"
    if (-not (Get-NetRoute -DestinationPrefix $prefix -InterfaceIndex $nic[0].InterfaceIndex -PolicyStore ActiveStore -ErrorAction SilentlyContinue)) {
        New-NetRoute -DestinationPrefix $prefix -InterfaceIndex $nic[0].InterfaceIndex -NextHop 0.0.0.0 -PolicyStore ActiveStore
    }
}
```

路由仅写入 ActiveStore，重启电脑后需重新执行；不修改默认路由。[New-NetRoute 官方说明](https://learn.microsoft.com/en-us/powershell/module/nettcpip/new-netroute)。仅接 STM32 时不需要这两条路由。

WSL mirrored 网络中用 `ip -br -4 addr` 查看对应 .2、.4 的接口，用 `ip route get 192.168.7.3` 确认走 .4 对应接口。若 Windows 的主机路由没有映射到 WSL，在 WSL 补充（接口名从实际地址识别）：

```bash
esp_if=$(ip -o -4 addr show | awk '$4 == "192.168.7.2/24" {print $2}')
stm_if=$(ip -o -4 addr show | awk '$4 == "192.168.7.4/24" {print $2}')
# 两个变量必须各自对应一个实际接口，否则先检查网卡地址。
test -n "$esp_if" && sudo ip route replace 192.168.7.1/32 dev "$esp_if" src 192.168.7.2
test -n "$stm_if" && sudo ip route replace 192.168.7.3/32 dev "$stm_if" src 192.168.7.4
```

在仓库根目录的 WSL 终端启动 Agent（WSL 需配置 mirrored 网络）：

```bash
export ROS_DOMAIN_ID=0
ping -c 3 192.168.7.3
bash Tools/scripts/agent.sh udp 8888 jazzy
```

RNDIS 网卡由 Windows 管理，mirrored WSL 使用同一网络；不需要将这个 USB 网络设备 attach 给 WSL。已有 UDP 8888 Agent 时直接复用，一个 UDP Agent 可以接两块板，不能重复绑定同一端口。也可使用已有的 [Windows 原生 Agent](../Tools/README.md)。

需要给 WSL 放行两块板时，在仓库根目录的管理员 PowerShell 中执行：

```powershell
.\Tools\scripts\configure_agent_firewall.ps1 -Target wsl -McuAddress 192.168.7.1,192.168.7.3
```

另开 WSL 终端检查应用：

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0
ros2 node list
ros2 topic echo /stm32/heartbeat
# 再开一个终端监听，随后从另一个终端发送：
ros2 topic echo /stm32/echo
ros2 topic pub --once /stm32/command std_msgs/msg/Int32 '{data: 123}'
```

`echo` 应收到 123。所有终端和固件都应使用同一个 ROS 域。

## UART 模式

菜单选择 `USART1 DMA`：PA9 TX、PA10 RX，默认 115200 8N1，USB-UART 交叉接线并共地，3.3 V 电平。RX 使用 DMA2 Stream2 Channel4 循环模式、DMA 半满/全满与 IDLE 接收、2 KiB StreamBuffer。UART 模式保持 HSI 16 MHz。HAL tick 使用 TIM14，FreeRTOS 使用 SysTick。

把 USB-UART 通过 usbipd attach 给 WSL，在仓库根目录执行：

```bash
export ROS_DOMAIN_ID=0
bash Tools/scripts/agent.sh /dev/ttyUSB0 jazzy
```

## 网络实现与内存

参考 mcu_test 的 RNDIS 使用方式，在本工程使用 TinyUSB + lwIP IPv4/UDP raw API。USB 任务统一处理 USB 回调和 lwIP，不在 USB 中断内操作 lwIP；micro-ROS 任务通过固定长度队列交换完整 UDP 数据包。发送超时不会强制复位 USB 的发送状态，避免覆盖未发送完的数据。

只编译以太网、ARP、IPv4、ICMP、UDP，未启用 TCP、DNS、socket 和额外 tcpip 线程。网络包和 USB 缓冲区保留在主 SRAM；64 KiB FreeRTOS 堆放到 CCM，任务栈和 CPU 分配使用这块内存。Debugger 可查看 `usb_network_stats` 的 USB 状态、DHCP 回复数、包数、丢包、队列峰值和最小剩余堆。

节点 `/stm32/stm32f407`，每秒发布 Int32 `heartbeat`，`command` 原值回传 `echo`，Agent 断线后重新创建实体。USB 使用不带串口帧封装的 XRCE 数据包，UART 使用串口帧封装；共享应用代码无需修改。

## micro-ROS 静态库与验证

静态库和配套头文件位于 `Middlewares/Third_Party/micro_ros/lib/jazzy`，本地已有构建产物可直接使用，库不提交 Git。需要重新生成时，在本目录的 WSL 终端执行：

```bash
bash ../Tools/scripts/setup_wsl.sh jazzy
bash scripts/build_micro_ros.sh jazzy
```

默认 `MICRO_ROS_DISTRO=jazzy`；其他发行版生成后用 `-DMICRO_ROS_DISTRO=<发行版>` 选择，`MICROROS_ROOT` 可以指向包含 `libmicroros.a` 与 `include/rcl/rcl.h` 的自定义目录。依赖来源记录在各第三方目录的 `UPSTREAM.json`，公共逻辑修改位置见 [仓库 README](../README.md)。

2026-10-08：Windows、WSL 的 menuconfig 已打开验证；UART / USB 在两边同一个 Debug、Release 预设中切换构建通过，菜单保存后自动重新配置、无效参数检查通过。Windows Release 的 USB 模式使用 Flash 100,552 B、主 SRAM 51,440 B、CCM 中预留 65,536 B 堆；UART 模式主 SRAM 92,824 B。记录见本机 `build/menuconfig_validation.json`。DHCP 和 RNDIS 边界、错误报文及随机输入测试通过 AddressSanitizer / UndefinedBehaviorSanitizer 检查。TinyUSB 的接收偏移/长度检查补丁记录在 `PATCHES.md`。

2026-10-08 实板验证：MCU .3、电脑及 Agent .4，Windows/WSL USB Release 构建及 ST-Link 烧录校验通过。修复引入的 TinyUSB 驱动中遗漏的 STM32 GCCFG/VBUS 初始化，并在 lwIP 中允许 DHCP 服务接收源地址为 0.0.0.0 的请求；此前只编译和报文解析测试通过，未发现这两个集成问题。修复后 Windows 自动加载 RNDIS 驱动（0483:5741）并通过 DHCP 获得 .4，Windows/WSL 的 MCU /32 路由和双 MCU UDP 8888 放行生效，WSL ping 两块板均三次成功。

同一个 WSL Agent 同时收到 ESP32、STM32 的 heartbeat、command/echo；真实 STM32 heartbeat 165、166、167 经电脑转发到 ESP32 command 后，ESP32 原值回传成功。测试采用 WSL 回环 UDP DDS profile，命令与限制见 [Tools README](../Tools/README.md)。记录在 `../Tools/build/dual_mcu/topics_validation.json`、本机 `build/usb_ipv4_validation.json` 和 `build/usb_dhcp_fix_flash.log`。此验证包含电脑转发，ESP32 固件尚未直接订阅 STM32 的 heartbeat。现有 STM32CubeMX `.ioc` 尚未同步手写 UART/RTOS/USB 集成，重新生成前应合并相关用户代码。
