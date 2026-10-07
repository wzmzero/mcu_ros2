#!/usr/bin/env bash
# Shared workspace can be overridden when mcu_ros2 is moved to another location.
resolve_host_root() {
    local root=$1
    if [[ -n "${MICRO_ROS_HOST_ROOT:-}" ]]; then
        HOST_ROOT=$(cd -- "$MICRO_ROS_HOST_ROOT" && pwd)
    elif [[ -d "$root/Tools/build/micro_ros" ]]; then
        HOST_ROOT=$(cd -- "$root/Tools" && pwd)
    elif [[ -d "$root/build/micro_ros" ]]; then
        HOST_ROOT=$(cd -- "$root" && pwd)
    elif [[ -d "$root/../build/micro_ros" ]]; then
        HOST_ROOT=$(cd -- "$root/.." && pwd)
    else
        HOST_ROOT=$(cd -- "$root/Tools" && pwd)
    fi
}
# Shared by native WSL builders; source this file, then call load_ros_env.
load_ros_env() {
    ROS_BUILD_DISTRO=${1:-${ROS_DISTRO:-}}
    if [[ -z "$ROS_BUILD_DISTRO" ]]; then
        local setups=()
        shopt -s nullglob
        setups=(/opt/ros/*/setup.bash)
        if (( ${#setups[@]} == 1 )); then
            ROS_BUILD_DISTRO=$(basename "$(dirname "${setups[0]}")")
        else
            echo 'Specify the installed ROS distribution or source its setup.bash.' >&2
            return 2
        fi
    fi
    [[ "$ROS_BUILD_DISTRO" =~ ^[a-z][a-z0-9_]*$ ]] || return 2
    local setup="/opt/ros/$ROS_BUILD_DISTRO/setup.bash"
    [[ -f "$setup" ]] || { echo "ROS setup not found: $setup" >&2; return 2; }
    set +u
    source "$setup"
    set -u
}
require_tools() {
    local tool
    for tool in "$@"; do
        command -v "$tool" >/dev/null || {
            echo "Missing $tool. Run: bash Tools/scripts/setup_wsl.sh $ROS_BUILD_DISTRO" >&2
            return 2
        }
    done
}
load_micro_ros_setup() {
    local root=$1
    local ws="$root/build/micro_ros/$ROS_BUILD_DISTRO/setup"
    require_tools git colcon rosdep vcs cmake make python3
    mkdir -p "$ws/src"
    if [[ ! -d "$ws/src/micro_ros_setup" ]]; then
        git clone --branch "$ROS_BUILD_DISTRO" --single-branch \
            https://github.com/micro-ROS/micro_ros_setup.git "$ws/src/micro_ros_setup"
    fi
    local branch
    branch=$(git -C "$ws/src/micro_ros_setup" branch --show-current)
    [[ "$branch" == "$ROS_BUILD_DISTRO" ]] || {
        echo "micro_ros_setup branch mismatch: $branch / $ROS_BUILD_DISTRO" >&2; return 2;
    }
    if [[ ! -f "$ws/install/local_setup.bash" ]]; then
        (
            cd "$ws"
            rosdep install --from-paths src --ignore-src -y --rosdistro "$ROS_BUILD_DISTRO"
            colcon build --packages-select micro_ros_setup
        )
    fi
    set +u
    source "$ws/install/local_setup.bash"
    set -u
}
