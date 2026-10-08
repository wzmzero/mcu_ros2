#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
distro="${1:-jazzy}"
domain="${2:-0}"
local_dds="${3:-1}"
[[ "$distro" =~ ^[a-z]+$ ]] || { echo 'Invalid ROS distribution' >&2; exit 2; }
[[ "$domain" =~ ^[0-9]+$ ]] && (( domain <= 232 )) || { echo 'Invalid ROS domain' >&2; exit 2; }
[[ -f "/opt/ros/$distro/setup.bash" ]] || { echo "ROS 2 $distro is not installed in this Linux environment" >&2; exit 2; }
# ROS setup scripts can refer to unset variables.
set +u
source "/opt/ros/$distro/setup.bash"
for overlay in \
    "$script_dir/../../../micro_ros/$distro/agent/install/local_setup.bash" \
    "$script_dir/../../build/micro_ros/$distro/agent/install/local_setup.bash"; do
    if [[ -f "$overlay" ]]; then source "$overlay"; break; fi
done
set -u
export ROS_DOMAIN_ID="$domain"
if [[ "$local_dds" == 1 ]]; then
    export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
    unset ROS_LOCALHOST_ONLY
    export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
    export ROS_STATIC_PEERS=127.0.0.1
    export FASTRTPS_DEFAULT_PROFILES_FILE="$script_dir/../../config/fastdds_wsl_local.xml"
    [[ -f "$FASTRTPS_DEFAULT_PROFILES_FILE" ]] || { echo 'DDS configuration file is missing' >&2; exit 2; }
    export FASTDDS_DEFAULT_PROFILES_FILE="$FASTRTPS_DEFAULT_PROFILES_FILE"
fi
exec "${ROS_WORKER_PYTHON:-python3}" -u "$script_dir/worker.py" "${@:4}"
