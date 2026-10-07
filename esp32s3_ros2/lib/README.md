# 平台库

本目录只保存 esp32s3_ros2 对应工具链的编译库。micro-ROS 库和配套头文件位于 micro_ros/<ROS 发行版>/libmicroros.a 和 micro_ros/<ROS 发行版>/include。生成的发行版目录已加入 .gitignore。

ESP32 的 Jazzy 正式库已生成并通过 Windows ESP-IDF 完整链接验证。重新生成时，先在 Windows 导出配置，再在 WSL 执行 `bash scripts/build_micro_ros.sh`；具体命令见上一级 README.md。
