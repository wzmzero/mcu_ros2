#!/usr/bin/env bash
# Native WSL build. No Docker daemon or image is required.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$root/../Tools/scripts/ros_env.sh"
resolve_host_root "$root/.."
load_ros_env "${1:-}"
require_tools arm-none-eabi-gcc arm-none-eabi-g++ arm-none-eabi-ar
load_micro_ros_setup "$HOST_ROOT"
ws="$root/build/micro_ros/$ROS_BUILD_DISTRO/library"
mkdir -p "$ws"
cd "$ws"
# micro_ros_setup creates an internal "firmware" build tree here, not at project root.
if [[ ! -f firmware/PLATFORM ]]; then
    [[ ! -e firmware ]] || { echo "Incomplete workspace: $ws/firmware. Use a fresh build directory." >&2; exit 2; }
    ros2 run micro_ros_setup create_firmware_ws.sh generate_lib
fi
[[ "$(head -n 1 firmware/PLATFORM)" == generate_lib ]] || {
    echo 'Workspace is not a generate_lib workspace.' >&2; exit 2;
}
[[ -d firmware/mcu_ws && -f firmware/dev_ws/install/setup.bash ]] || {
    echo 'Incomplete micro-ROS firmware workspace.' >&2; exit 2;
}
export TOOLCHAIN_PREFIX
TOOLCHAIN_PREFIX=$(command -v arm-none-eabi-gcc)
TOOLCHAIN_PREFIX=${TOOLCHAIN_PREFIX%gcc}
export RET_CFLAGS="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os -ffunction-sections -fdata-sections"
config="$root/Middlewares/Third_Party/micro_ros/tools/microros_static_library/library_generation"
ros2 run micro_ros_setup build_firmware.sh "$config/toolchain.cmake" "$config/colcon.meta"
generated="$ws/firmware/build"
[[ -s "$generated/libmicroros.a" && -f "$generated/include/rcl/rcl.h" ]] || {
    echo 'Native builder did not generate a complete library.' >&2; exit 1;
}
output="$root/Middlewares/Third_Party/micro_ros/lib/$ROS_BUILD_DISTRO"
if [[ -f "$output/ROS_DISTRO" && "$(cat "$output/ROS_DISTRO")" != "$ROS_BUILD_DISTRO" ]]; then
    echo "Existing library is for another ROS distribution: $output. Preserve or remove it before replacing." >&2
    exit 2
fi
mkdir -p "$output/include"
cp "$generated/libmicroros.a" "$output/libmicroros.a"
cp -a "$generated/include/." "$output/include/"
printf '%s\n' "$ROS_BUILD_DISTRO" > "$output/ROS_DISTRO"
git -C "$HOST_ROOT/build/micro_ros/$ROS_BUILD_DISTRO/setup/src/micro_ros_setup" rev-parse HEAD > "$output/MICRO_ROS_SETUP_COMMIT"
arm-none-eabi-size "$output/libmicroros.a" > "$output/archive_size.txt"
echo "Native micro-ROS $ROS_BUILD_DISTRO library installed at $output"
echo 'Windows: cmake --preset Release; cmake --build --preset Release'
