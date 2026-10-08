# 两板原生 ROS 2 通信

ESP32 与 STM32 都是 micro-ROS Client。Agent 为它们建立 DDS 实体；发布端与订阅端的完整 topic 名、消息类型和 QoS 匹配，并且 ROS_DOMAIN_ID 相同，就能通信。电脑无需写消息转发节点。当前一个 UDP Agent 同时服务两块板，MCU 间的数据路径为：

```text
ESP32 publisher → XRCE UDP → Agent/DDS → XRCE UDP → STM32 subscription
STM32 publisher → XRCE UDP → Agent/DDS → XRCE UDP → ESP32 subscription
```

客户端仍需在程序里创建 subscription 和回调，DDS 不会自动把每个 topic 交给所有应用。示例在 [app/communication_demo.c](app/communication_demo.c)，两板共用源码，各自的 `micro_ros_platform.c` 指定对端 namespace。

## 编译启用

STM32 在项目目录执行 `cmake --build --preset Release --target menuconfig`，选择 **Topic/service/action peer communication demo**。也可执行：

```bash
python3 scripts/menuconfig.py --set ROS_COMM_DEMO=y
bash scripts/build_micro_ros.sh jazzy
```

回到 Windows STM32 编译环境，执行 `cmake --preset Release`、`cmake --build --preset Release` 和 `cmake --build --preset Release --target flash_swd`。

ESP32 在 `idf.py -DMICROROS_PREPARE=ON menuconfig` 的应用菜单中选择同名选项，随后按 [ESP32 README](../esp32s3_ros2/README.md) 的 Windows 配置 → WSL 生成库 → Windows 构建/烧录流程执行。对应配置项为 `CONFIG_APP_ROS_COMM_DEMO`。

两板各使用一个 sdkconfig。示例默认关闭，关闭时不编译 action/service 示例实现。启用之前必须用仓库更新后的 colcon.meta 重建两边的库；RMW 池上限为 10 publishers、5 subscriptions、4 services、4 clients、12 history slots。Action server 内部需要 3 services、2 publishers，action client 需要 3 clients、2 subscriptions，不能按一个普通 topic 计算资源。

STM32 在 USB 模式或启用此示例时，将 64 KB FreeRTOS 堆与任务栈放在 CCMRAM，避免主 SRAM 溢出；UART RX DMA 和 USB 数据缓冲区保留在 DMA 可访问的主 SRAM。默认 UART 基础模式保留原来的堆布局。

启用全部通信示例时，publisher 池正好占满；新增传感器 topic 前需提高 `RMW_UXRCE_MAX_PUBLISHERS` 并重新生成库。STM32 修改 library_generation/colcon.meta，ESP32 修改应用级 app-colcon.meta。

## Topic 回环

两板的 namespace 分别是 `/esp32s3`、`/stm32`，以下 `<本板>`、`<对端>` 用其中一个替换。全部使用 `std_msgs/msg/Int32` 和默认 reliable QoS。

| 接口 | 行为 |
|---|---|
| `/<本板>/heartbeat` | 每秒发布本板递增序号 |
| 订阅 `/<对端>/heartbeat` | 将收到的对端序号发布到 `/<本板>/peer_received` |
| 订阅 `/<对端>/peer_received` | 将对端确认过的本板序号发布到 `/<本板>/roundtrip` |
| `/<本板>/command`、`/<本板>/echo` | 保留原来的单板命令回显 |

例如 `/esp32s3/heartbeat=123` → STM32 订阅 → `/stm32/peer_received=123` → ESP32 订阅 → `/esp32s3/roundtrip=123`。反方向也同时运行。roundtrip 不再触发新的转发，因此没有无限回环。

Agent ping 只能确认 Agent 在线。示例另行监测对端 heartbeat 与回环确认：对端持续在线但确认中断 10 秒时，重建本板 ROS 实体；未完成的 action 请求超过 15 秒也会重建。这用于恢复单板重启后的订阅/请求路径，正在进行的示例 action 可能随重建终止。

网络客户端的 XRCE 标识每次启动重新生成：ESP32 使用 esp_random，STM32 使用硬件 RNG 混合芯片 UID。实测固定 STM32 标识在重启后会留下旧的订阅路径；新的启动标识配合回环监测可恢复通信。STM32 RNG 不可用时保留 UID 回退值。

## Service

两板均提供 `/<本板>/add_two_ints`，类型 `example_interfaces/srv/AddTwoInts`；同时作为 client，每五秒请求对端计算 `20+22`，在 `/<本板>/service_result` 发布 `std_msgs/msg/Int64=42`。请求关联 sequence number，迟到的旧响应不会当作新请求的结果。服务端在 int64 溢出时返回最大/最小值，示例测试包含负数、大于 32 位的值和上下界饱和。

```bash
ros2 service call /stm32/add_two_ints example_interfaces/srv/AddTwoInts '{a: 20, b: 22}'
```

## Action

两板均提供 `/<本板>/fibonacci`，类型 `example_interfaces/action/Fibonacci`；同时作为 client 请求对端。`order` 表示结果序列长度，支持 2～10，超出范围拒绝目标。order=6 的结果为 `[0,1,1,2,3,5]`。

服务端最多接收两个并发目标，序列使用固定 10 元素缓冲区，在 ROS 任务中每 200 ms 推进一步，不在回调内阻塞等待。client 每五秒交替发送 order=6 和 order=10，后者收到至少 4 元素的 feedback 时申请取消。`action_feedback` 发布反馈长度，`action_result` 发布最终序列最后一个数，`action_status` 发布 ROS action 状态码（成功 4、取消 5）。这些诊断 topic 使用 Int32。

```bash
ros2 action send_goal /esp32s3/fibonacci example_interfaces/action/Fibonacci '{order: 6}' --feedback
```

改成实际任务时，在固定周期步骤里调用相应驱动，并定义适合任务的 goal、feedback 和 result。

## 实板自动测试

两板烧录并连接同一 Agent 后，在仓库根目录的 WSL 终端执行（网络配置见 [Tools README](../Tools/README.md)）：

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=0
export FASTRTPS_DEFAULT_PROFILES_FILE="$(pwd)/Tools/config/fastdds_wsl_local.xml"
python3 Tools/tests/test_mcu_protocols.py
```

Agent 也要使用同一 profile。已有 UDP 8888 Agent 时直接复用。该 profile 把电脑侧 DDS 限制在本机 WSL 回环接口，适合当前测试；MCU 的 XRCE UDP 地址保持 ESP32 `.1→.2:8888`、STM32 `.3→.4:8888`。跨电脑 DDS 需改用可达网络接口的 DDS 配置。

测试脚本没有 topic publisher，不会代替板端做消息转发。它观察两板至少三个不同序号的完整回环，以及两板发起的 service/action 成功和取消；另外调用两板 service，并对两板 action 分别测试反馈、结果、拒绝、取消。缺少任一板端路径会失败，结果写到 `Tools/build/dual_mcu/protocols_validation.json`。

2026-10-08 实板验证：Windows 构建并烧录 ESP32-S3、STM32F407VET6，WSL 单个 UDP 8888 Agent 接收两板。上述 topic、service、action 测试均通过。保持 Agent 与 ESP32 运行，单独复位 STM32 后，全套测试再次通过；重启报告位于 `Tools/build/dual_mcu/protocols_reboot_validation.json`。测试先等待真实回环与板间请求恢复，避免对重启前的残留 graph 实体发送请求，且完成时检查三秒以内的 topic 活跃度。

最终 STM32 USB 示例占用 Flash 173,776 B、主 SRAM 99,000 B、CCMRAM 堆 65,536 B；UART 示例编译占用 Flash 156,368 B、主 SRAM 74,856 B，UART 本次仅编译验证。关闭示例的 UART 基础模式也编译通过。库、构建产物和实测报告不提交到 Git。

## 传感器消息

常用标准接口已经在 sensor_msgs 中定义，本项目生成的库包含该包。消息负责数据结构与序列化，传感器硬件驱动和采样仍需自己实现。官方类型列表：[sensor_msgs/msg](https://github.com/ros2/common_interfaces/tree/jazzy/sensor_msgs/msg)。

| 数据 | 常用标准类型 | 主要单位/约定 |
|---|---|---|
| IMU | `sensor_msgs/msg/Imu` | 角速度 rad/s，加速度 m/s²；旋转用四元数，未知协方差按接口定义填写 |
| 温度、湿度 | `Temperature`、`RelativeHumidity` | 摄氏度；相对湿度 0～1 |
| 距离 | `Range` | 米，注明红外/超声及量程、视场角 |
| 磁场、气压 | `MagneticField`、`FluidPressure` | 特斯拉、帕斯卡 |
| 光照、GNSS | `Illuminance`、`NavSatFix` | lux；经纬度以度表示 |
| 电池 | `BatteryState` | 电压、电流、容量和状态 |
| 编码器/关节 | `JointState` | 转动关节位置 rad，线性关节位置 m |
| 激光、图像、点云 | `LaserScan`、`Image`、`PointCloud2` | 数据量较大，STM32F407 需限制长度、速率和缓冲区 |

基本流程为 `I2C/SPI/ADC 采样 → 单位换算 → 填写标准 msg → rcl_publish()`。带 Header 的消息需正确填写 `frame_id` 和时间戳；如需 ROS 时间，可在 Agent 连接后做 micro-ROS 时间同步。含字符串或可变长数组的消息必须预分配足够容量，不能只声明一个未初始化的结构体就发布。

没有连接真实传感器时，通信测试无法证明传感器测量正确。只有标准消息无法表达业务数据时才新增自定义 `.msg`/`.srv`/`.action`；自定义接口需在 MCU 静态库和电脑 ROS 工作区两端生成，类型名称与字段定义必须相同。
