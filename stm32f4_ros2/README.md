# stm32f4_ros2

STM32F407VET6 + FreeRTOS + Jazzy micro-ROS。公共应用位于 `../firmware/app`，本工程 `Src` 提供硬件和传输适配。可以在编译前选择 USART1 DMA 或原生 USB RNDIS + UDP。

## Windows 构建与 menuconfig

安装 STM32CubeCLT，将其 CMake、Ninja、GNU-tools-for-STM32 的 `bin` 加入 PATH；需要 Python 3 和 pip。在 Windows PowerShell 或 CLion 的终端中进入本目录：

```powershell
cmake --preset Release
cmake --build build/Release --target menuconfig
cmake --build --preset Release
cmake --build build/Release --target flash_swd
```

菜单采用 [Kconfiglib](https://github.com/ulfalizer/Kconfiglib)，操作与 ESP-IDF menuconfig 类似：方向键移动，Enter 进入/编辑，Space 选择，S 保存，Q 退出。可配置传输方式、ROS 域、串口波特率；选择 USB RNDIS 后还可配置晶振频率、MCU/主机地址及 Agent 地址、端口。首次运行自动把 Kconfiglib 和 Windows 菜单所需的 windows-curses 安装到本工程 `.tools/menuconfig`，不需要 Linux 环境。

配置保存到 `sdkconfig`，构建目录生成 `generated/sdkconfig.h` 和 CMake 参数。保存菜单后直接 build，会自动重新配置并编译对应传输，不需要清空 build。`board.h` 引入生成的头文件，应用可以使用 `CONFIG_*` 宏。

`Debug` / `Release` 共享 `sdkconfig`，第一次默认为 UART。另提供 `USB-Debug` / `USB-Release`，使用独立的 `sdkconfig.usb`，第一次默认为 USB RNDIS。**已有 sdkconfig 的选择优先于 preset 和 `-D` 参数**，以后用菜单修改。配置文件是本机配置，不提交 Git。

不通过菜单修改参数的示例：

```powershell
python scripts/menuconfig.py --config sdkconfig --set ROS_DOMAIN_ID=1
cmake --build --preset Release
```

产物为 `build/<preset>/stm32f4_ros2.elf`、`.bin`、`.hex`，链接地址 `0x08000000`。`flash_swd` 在找到 STM32CubeProgrammer CLI 后可用，需要 ST-Link。

## USB RNDIS 模式

```powershell
cmake --preset USB-Release
cmake --build build/USB-Release --target menuconfig
cmake --build --preset USB-Release
cmake --build build/USB-Release --target flash_swd
```

连接 MCU 原生 USB：PA11 为 D-，PA12 为 D+；CH340 和 ST-Link 接口不能提供 RNDIS。默认外部晶振 8 MHz，沿用 `C:\Users\admin\Desktop\mcu_test` 的板级参考。若实板晶振不同，先在菜单中修改。USB 模式通过 PLL 配置 CPU 168 MHz、USB 48 MHz。

| 参数 | 默认值 |
| --- | --- |
| MCU 地址 | 192.168.8.1/24 |
| Windows USB 网卡地址，板端 DHCP 分配 | 192.168.8.2/24 |
| Agent 地址、端口 | 192.168.8.2:8888 |
| ROS_DOMAIN_ID | 0 |

STM32 使用 192.168.8.0/24，ESP32 原生 USB 使用 192.168.7.0/24，避免两块板同时连接时路由冲突。DHCP 不发布默认网关和 DNS。Windows USB 网卡应保持自动获取地址，也可以按表手动设置。

在仓库根目录的 WSL 终端启动 Agent（WSL 需配置 mirrored 网络）：

```bash
export ROS_DOMAIN_ID=0
ping -c 3 192.168.8.1
bash Tools/scripts/agent.sh udp 8888 jazzy
```

RNDIS 网卡由 Windows 管理，mirrored WSL 使用同一网络；不需要将这个 USB 网络设备 attach 给 WSL。已有 UDP 8888 Agent 时直接复用，一个 UDP Agent 可以接两块板，不能重复绑定同一端口。也可使用已有的 [Windows 原生 Agent](../Tools/README.md)。

需要给 WSL 放行两块板时，在仓库根目录的管理员 PowerShell 中执行：

```powershell
.\Tools\scripts\configure_agent_firewall.ps1 -Target wsl -McuAddress 192.168.7.1,192.168.8.1
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

2026-10-08：Windows 的 menuconfig 已打开验证；UART / USB 的 Debug 和 Release 构建通过，菜单保存后自动重新配置并切换传输、无效参数检查通过。USB Release 使用 Flash 100,552 B、主 SRAM 51,440 B、CCM 中预留 65,536 B 堆；UART Release 主 SRAM 92,824 B。DHCP 和 RNDIS 边界、错误报文及随机输入测试通过 AddressSanitizer / UndefinedBehaviorSanitizer 检查。TinyUSB 的接收偏移/长度检查补丁记录在 `PATCHES.md`。

USB Release 已通过 ST-Link 烧录并校验；目前电脑尚未枚举到 STM32 原生 USB 网卡，STM32 的 DHCP、ping、heartbeat/echo 实板链路待接上原生 USB 后验证。现有 STM32CubeMX `.ioc` 尚未同步手写 UART/RTOS/USB 集成，重新生成前应合并相关用户代码。
