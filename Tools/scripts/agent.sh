#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
usage() {
    echo 'Usage: bash Tools/scripts/agent.sh /dev/ttyACM0 [ROS_DISTRO]' >&2
    echo '       bash Tools/scripts/agent.sh udp [PORT=8888] [ROS_DISTRO]' >&2
}
[[ $# -gt 0 ]] || { usage; exit 2; }
mode=$1
if [[ "$mode" == udp ]]; then
    port=${2:-8888}
    [[ "$port" =~ ^[0-9]{1,5}$ ]] && (( 10#$port >= 1 && 10#$port <= 65535 )) || {
        echo 'UDP port must be between 1 and 65535.' >&2; exit 2;
    }
    port=$((10#$port))
    distro=${3:-}
    [[ $# -le 3 ]] || { usage; exit 2; }
else
    port=$mode
    distro=${2:-}
    [[ $# -le 2 ]] || { usage; exit 2; }
fi
source "$root/Tools/scripts/ros_env.sh"
resolve_host_root "$root"
load_ros_env "$distro"
overlay="$HOST_ROOT/build/micro_ros/$ROS_BUILD_DISTRO/agent/install/local_setup.bash"
if [[ -f "$overlay" ]]; then
    set +u
    source "$overlay"
    set -u
fi
if [[ "$mode" != udp ]]; then
    [[ -c "$port" ]] || { echo "Serial device not found: $port (attach USB to WSL first)." >&2; exit 2; }
    [[ -r "$port" && -w "$port" ]] || { echo "No read/write access to $port; check dialout group and device permissions." >&2; exit 2; }
fi
ros2 pkg prefix micro_ros_agent >/dev/null 2>&1 || {
    echo "Agent missing. Run: bash Tools/scripts/build_agent.sh $ROS_BUILD_DISTRO" >&2; exit 2;
}
if [[ "$mode" == udp ]]; then
    exec ros2 run micro_ros_agent micro_ros_agent udp4 --port "$port" -v 6
fi
exec ros2 run micro_ros_agent micro_ros_agent serial --dev "$port" -b 115200 -v 6
