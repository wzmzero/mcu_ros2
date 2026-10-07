# STM32 micro-ROS 第三方依赖

与 FreeRTOS 并列归入 Middlewares/Third_Party；两个子目录职责如下。

- tools：官方 micro_ros_stm32cubemx_utils 的移植及库生成工具。本工程保留必要的 microros_static_library/library_generation 配置、许可证和 UPSTREAM.json。tools/README.md 是上游原文，其中示例路径使用上游仓库目录名；本工程路径以上层 scripts 和 cmake 配置为准。
- lib/<ROS 发行版>：为 Cortex-M4F、hard-float 编译的 libmicroros.a，以及同次生成的 include、ROS_DISTRO 和来源信息。当前为 Jazzy。固件直接链接这里。

在 stm32f4_ros2 根目录执行：

```bash
bash scripts/build_micro_ros.sh jazzy
```

该原生 WSL 脚本读取 tools 中的生成配置，把结果安装到 lib/jazzy。可选 Docker 脚本先使用官方工具生成缓存，再安装到同一路径。

生成的库目录已加入 .gitignore。应用 elf/bin/hex 留在 stm32f4_ros2/build；两板共用的应用源码仍在 mcu_ros2/firmware。
