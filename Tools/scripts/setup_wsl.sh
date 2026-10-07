#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/Tools/scripts/ros_env.sh"
load_ros_env "${1:-}"
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    git build-essential cmake ninja-build rsync \
    python3-colcon-common-extensions python3-rosdep python3-vcstool \
    gcc-arm-none-eabi libnewlib-arm-none-eabi
if [[ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]]; then
    sudo rosdep init
fi
rosdep update --rosdistro "$ROS_BUILD_DISTRO"
echo "Native WSL tools ready for ROS $ROS_BUILD_DISTRO. Docker is not needed."
