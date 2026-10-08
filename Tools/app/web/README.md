# ROS 2 浏览器工作台

Windows 浏览器负责显示和交互，WSL / Linux 服务负责 ROS 2 和 Agent。前端无需 Qt、ROS 安装、Node.js 或 npm；静态 HTML / CSS / JavaScript 由后端提供。现有 Qt 工具仍可独立运行，使用相同 ROS JSON 协议。

```text
浏览器 / 自定义前端
          ↕ WebSocket JSON，/ws
runtime/web/server.py            HTTP + WebSocket 服务，管理客户端和 ROS 子进程
          ↕ 内部 JSON-lines
runtime/ros2/worker.py           rclpy：Topic、Service、Action、传感器、ROS 图
          ↕ DDS
micro-ROS Agent ↔ ESP32-S3 / STM32
```

## 安装与运行

WSL 中需要现有的 ROS 2 Jazzy 环境和 micro-ROS Agent。ROS 依赖与 [Qt 工作台](../qt_host/README.md) 相同；Web 服务另需 aiohttp。无需编译 Qt。

从工程根目录，在 WSL 中一次性安装服务依赖：

```bash
python3 -m venv Tools/build/web/venv
Tools/build/web/venv/bin/pip install -r Tools/runtime/web/requirements.txt
```

缺少 venv 时安装 `sudo apt install python3-venv`。也可直接安装系统包 `sudo apt install python3-aiohttp`，无需创建 venv。脚本优先使用 `Tools/build/web/venv/bin/python`，或通过 `ROS_WEB_PYTHON` 指定服务解释器。ROS 子进程默认使用 `/usr/bin/python3` 并自动加载 `/opt/ros/jazzy/setup.bash` 和 Agent overlay；独立的服务 venv 不需要安装 rclpy。自定义 ROS Python 环境可通过 `ROS_WORKER_PYTHON` 指定。

WSL / Linux 启动：

```bash
bash Tools/scripts/web.sh
# 其他配置：
bash Tools/scripts/web.sh --distro jazzy --domain 0 --port 8765
```

Windows PowerShell 启动同一个 WSL 服务：

```powershell
.\Tools\scripts\web.ps1
# 多个 WSL 发行版时：
.\Tools\scripts\web.ps1 -WslDistribution Ubuntu-24.04 -Domain 0 -Port 8765
# 只检查实际脚本路径和参数，不启动服务：
.\Tools\scripts\web.ps1 -Port 18765 -Check
```

Windows 浏览器打开 **http://localhost:8765**，点击“连接工作台”。默认启动 / 复用 UDP 8888 Agent；端口需匹配固件。服务默认监听 WSL 的 `127.0.0.1`，通过 WSL localhost 转发访问。若本机 WSL 禁用了 localhost 转发，先恢复相应网络设置。

ROS Domain / 发行版在服务启动时选择，所有页面共享该 ROS 环境。默认 DDS 仅本机，与已有 WSL Agent 配置一致；`--network-dds` / PowerShell `-NetworkDds` 使用 Linux 环境的网络 DDS 配置。

如需调整消息名称，可在命令最后使用标准 ROS remapping，例如 `--ros-args -r /esp32s3/command:=/my_board/command`。两块实板当前已验证 Domain 0；其他 Domain 的网络发现需按环境另行验证。

## 页面与生命周期

- 设备与控制：两板遥测、3.5 秒心跳在线判断、Int32 command 发布、完整 Int64 求和、Fibonacci Action 反馈 / 取消、Agent 管理。
- 传感器：刷新并选择标准 `sensor_msgs/msg/*` 话题；每个 WebSocket 客户端各有一个独立订阅。预览最多 10 Hz，长数组前 64 项。
- ROS 图：查看节点、Topic 类型与 Service。
- 事件记录：最近 200 条，可清空和导出。

服务创建一个 ROS worker，多页面共享板卡遥测与 Agent；请求结果和 Action 反馈只发给发起页面。请求 ID 在服务内部按客户端隔离。每块板同时允许一个上位机 Action；其他页面不能取消它。

关闭 / 刷新页面会取消该页面的 Action、移除其传感器订阅，后端和 Agent 继续运行。按 Ctrl+C 停止服务会清理 ROS 子进程和服务创建的 Agent，保留复用的外部 Agent。Agent 停止按钮作用于共享服务，只能停止服务创建的实例。

连接就绪不代表 MCU 在线。仅收到实际 heartbeat 才显示在线，重连后等待新心跳；不重放缓存心跳。Action / Service 的 ROS 超时为 12 秒，前端请求超时为 15 秒。慢客户端输出队列满时会断开，保护其他客户端。ROS 子进程异常会通知页面并使 `/health` 返回 503，需要重启服务。

## 接入自己的前端

可使用 Vue、React 或普通 JavaScript。复用 [ros-client.js](assets/ros-client.js)，也可直接使用标准 WebSocket。SDK 不引用 DOM 或任何界面框架。

```javascript
import {RosClient} from './ros-client.js';
const ros = new RosClient();
ros.addEventListener('event', ({detail}) => {
  if (detail.event === 'ready') {
    ros.request('graph').promise.then(console.log).catch(console.error);
  }
  if (detail.event === 'telemetry') console.log(detail.board, detail.field, detail.value);
});
ros.connect('ws://localhost:8765/ws');
// 在 ready 事件后请求；Action 的返回 id 可用于 cancel 的 goal_id。
// ros.request('publish', {board:'esp32s3', value:'42'}).promise
```

独立前端开发服务器需要显式允许其 Origin，例如：

```bash
bash Tools/scripts/web.sh --allow-origin http://localhost:5173
```

浏览器来源必须与服务同源或匹配 `--allow-origin`；无 Origin 的脚本客户端可连接。当前服务用于本机开发，没有登录鉴权；外网部署需由有鉴权的 HTTPS / WebSocket 代理提供访问。`--host` 可指定监听地址，跨电脑访问还需要配置 WSL 网络和防火墙。

### JSON 协议 v1

每条消息是一个 JSON 对象。请求 `id` 是 1–128 字符的字符串，在当前连接的未完成请求中必须唯一。`board` 为 `esp32s3` 或 `stm32`。

| op | 请求字段 | 结果 |
| --- | --- | --- |
| `graph` | 无 | `nodes`、`topics`、`services` |
| `publish` | `board`、`value`（建议十进制字符串） | `published` |
| `service` | `board`、`a`、`b`（十进制字符串） | `sum`（字符串，保留 Int64 精度） |
| `action` | `board`、`order`（Int32） | `accepted`、`status`、`sequence`；期间发送反馈 |
| `cancel` | `board`、`goal_id`（同连接的 Action 请求 id） | `cancel_requested` |
| `subscribe` | `topic`、`type`（`sensor_msgs/msg/*`） | `topic`、`type` |
| `agent_start` | `port`（1–65535） | Agent 状态 |
| `agent_stop` | 无 | Agent 状态；禁止停止外部实例 |
| `agent_status` | 无 | Agent 状态 |

示例请求与事件：

```json
{"id":"1","op":"publish","board":"esp32s3","value":"42"}
{"event":"response","id":"1","op":"publish","board":"esp32s3","ok":true,"result":{"published":42}}
{"event":"telemetry","board":"esp32s3","field":"echo","value":"42"}
```

事件：`ready`（含 `protocol:1`、`domain`、`boards`）、`response`（失败含 `ok:false,error`）、`telemetry`、`agent`、`agent_log`、`log`、`sensor`、`action_accepted`、`action_feedback`、`backend_error`。`stop` 和内部客户端释放操作不对 WebSocket 客户端开放。

## 验证

真实 HTTP / WebSocket / ROS 集成测试使用 **Domain 0、`/web_fixture/*` 名称空间的电脑夹具**，无需实板；所有板卡接口通过 ROS remapping 指向夹具，不发送到 MCU 的话题、服务或 Action。测试覆盖多客户端隔离、Topic、完整 Int64 Service、Action 反馈 / 取消、断开清理和服务退出。

```bash
source /opt/ros/jazzy/setup.bash
Tools/build/web/venv/bin/python Tools/app/web/tests/test_gateway.py
```

浏览器验证额外需要 Playwright 和 Chromium（仅测试依赖）：

```bash
Tools/build/web/venv/bin/pip install playwright
Tools/build/web/venv/bin/playwright install chromium
source /opt/ros/jazzy/setup.bash
Tools/build/web/venv/bin/python Tools/app/web/tests/test_browser.py
```

测试生成 `Tools/build/web/gateway_report.json`、`browser_report.json`、`desktop.png`、`mobile.png`。夹具模拟设备接口，使用真实 ROS 类型和通信；截图中的数值不代表实板。现有 Qt 后端回归测试仍可使用 `python3 Tools/app/qt_host/tests/test_worker.py`。

两个 Web 测试共享夹具名称空间，应顺序运行；使用现有 WSL 本机 DDS 配置。

浏览器测试默认使用 HTTP 18766，集成测试使用 18767；可通过 `ROS_WEB_TEST_HTTP_PORT` 调整。使用固定端口可避免 WSL mirrored 网络中部分动态端口被 Windows 占用或转发的影响。

需要验证真实 MCU 时，先启动 Domain 0 的 Web 服务，再显式执行以下测试（要求两块板运行现有通信 demo，会发送 ESP32 command=57007、STM32 command=57008）：

```bash
Tools/build/web/venv/bin/python Tools/app/web/tests/test_hardware.py
# 自定义服务端口：
Tools/build/web/venv/bin/python Tools/app/web/tests/test_hardware.py --url http://localhost:18765/ws
```

实板测试检查两板各三个不同心跳、command/echo、完整 Int64 求和、Action 成功 / 拒绝 / 取消，报告为 `Tools/build/web/hardware_report.json`。测试不关闭共享服务和复用的外部 Agent。

2026-10-08 本机验证：WebSocket / ROS 集成、浏览器桌面与手机布局、断开 / 重连 / 页面刷新，以及原有 Qt 后端协议回归通过。两块真实 MCU 的 WebSocket Topic / Service / Action 测试通过，复用现有 WSL Agent。默认 8765 在本机曾被占用，当前服务使用 18765；端口占用时可用 `--port` 或 PowerShell `-Port` 调整。
