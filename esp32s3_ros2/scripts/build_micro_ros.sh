#!/usr/bin/env bash
# WSL library generation with the SDK/configuration exported by Windows IDF.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
unset AMENT_PREFIX_PATH CMAKE_PREFIX_PATH COLCON_PREFIX_PATH ROS_DISTRO ROS_VERSION ROS_PYTHON_VERSION PYTHONPATH
exec python3 "$root/scripts/micro_ros_bridge.py" build "${1:-$root/build/micro_ros_request.json}"
