#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/Tools/scripts/ros_env.sh"
resolve_host_root "$root"
load_ros_env "${1:-}"
load_micro_ros_setup "$HOST_ROOT"
ws="$HOST_ROOT/build/micro_ros/$ROS_BUILD_DISTRO/agent"
mkdir -p "$ws"
cd "$ws"
if [[ ! -d src/uros/micro-ROS-Agent ]]; then
    ros2 run micro_ros_setup create_agent_ws.sh
fi
fastdds="/opt/ros/$ROS_BUILD_DISTRO/share/fastrtps/cmake"
fastcdr="/opt/ros/$ROS_BUILD_DISTRO/lib/cmake/fastcdr"
[[ -f "$fastdds/fastrtps-config.cmake" && -f "$fastcdr/fastcdr-config.cmake" ]] || {
    echo 'ROS Fast DDS/Fast CDR CMake packages not found; inspect the ROS installation.' >&2
    exit 2
}
# Forward dependency isolation into the nested XRCE ExternalProject as well.
python3 - "$ws" <<'PY'
from pathlib import Path
import sys
workspace = Path(sys.argv[1]).resolve()
source = workspace / 'src/uros/micro-ROS-Agent/micro_ros_agent/cmake/SuperBuild.cmake'
content = source.read_text()
marker = '# ros2_stm32f4: isolate nested DDS dependencies'
anchor = '                -DCMAKE_PREFIX_PATH:PATH=<INSTALL_DIR>'
if marker not in content:
    if content.count(anchor) != 1:
        raise SystemExit('Unexpected upstream SuperBuild.cmake; cannot safely patch dependency paths.')
    content = content.replace(anchor, anchor + '''
                # ros2_stm32f4: isolate nested DDS dependencies
                -DCMAKE_IGNORE_PREFIX_PATH:STRING=${CMAKE_IGNORE_PREFIX_PATH}
                -DCMAKE_FIND_USE_PACKAGE_REGISTRY:BOOL=OFF
                -Dfastrtps_DIR:PATH=${fastrtps_DIR}
                -Dfastcdr_DIR:PATH=${fastcdr_DIR}''')
    source.write_text(content)
# Also update a workspace patched by an earlier version of this script.
content = source.read_text()
logger_arg = '                -Dspdlog_DIR:PATH=${spdlog_DIR}'
if logger_arg not in content:
    anchor = '                -Dfastcdr_DIR:PATH=${fastcdr_DIR}'
    if content.count(anchor) != 1:
        raise SystemExit('Unexpected dependency patch; cannot safely add the logger path.')
    source.write_text(content.replace(anchor, anchor + '\n' + logger_arg))
PY
case "${2:-}" in
    --clean)
        # Only regenerable Agent artifacts; do not touch /usr/local or the MCU library.
        python3 - "$ws" <<'PY'
from pathlib import Path
import shutil, sys
workspace = Path(sys.argv[1]).resolve()
for relative in ('build/micro_ros_agent', 'install/micro_ros_agent'):
    target = workspace / relative
    resolved = target.resolve()
    if workspace not in resolved.parents or target.is_symlink():
        raise SystemExit('Refusing an Agent artifact path outside the workspace.')
    if target.exists():
        print('Cleaning Agent artifact:', target)
        shutil.rmtree(target)
PY
        ;;
    "") ;;
    *) echo 'Usage: bash Tools/scripts/build_agent.sh [ROS_DISTRO] [--clean]' >&2; exit 2 ;;
esac
# XRCE 2.4.3 expects spdlog 1.9.2 with its bundled fmt. Keep it local to
# this workspace so /usr/local headers and Ubuntu's newer fmt cannot mix.
logger_src="$ws/dependencies/spdlog"
logger_build="$ws/dependencies/build/spdlog"
logger_prefix="$ws/dependencies/install/spdlog"
if [[ ! -d "$logger_src/.git" ]]; then
    mkdir -p "$(dirname "$logger_src")"
    git clone --branch v1.9.2 --depth 1 --single-branch \
        https://github.com/gabime/spdlog.git "$logger_src"
fi
[[ "$(git -C "$logger_src" describe --tags --exact-match)" == v1.9.2 ]] || {
    echo 'Unexpected spdlog revision; expected v1.9.2.' >&2; exit 2;
}
cmake -S "$logger_src" -B "$logger_build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    "-DCMAKE_INSTALL_PREFIX=$logger_prefix" \
    -DCMAKE_IGNORE_PREFIX_PATH:STRING=/usr/local \
    -DBUILD_SHARED_LIBS=OFF -DSPDLOG_BUILD_SHARED=OFF -DSPDLOG_FMT_EXTERNAL=OFF \
    -DSPDLOG_FMT_EXTERNAL_HO=OFF -DSPDLOG_BUILD_EXAMPLE=OFF \
    -DSPDLOG_BUILD_TESTS=OFF -DSPDLOG_BUILD_BENCH=OFF -DSPDLOG_INSTALL=ON
cmake --build "$logger_build" --parallel 2
cmake --install "$logger_build"
spdlog="$logger_prefix/lib/cmake/spdlog"
[[ -f "$spdlog/spdlogConfig.cmake" ]] || {
    echo 'Workspace spdlog CMake package not found.' >&2; exit 2;
}
# Binding this directory prevents a previously installed /usr/local Agent from
# importing another Fast DDS export set into the micro-ROS Agent configure step.
colcon build --packages-up-to micro_ros_agent --cmake-clean-cache --cmake-args \
    -DUAGENT_BUILD_EXECUTABLE:BOOL=OFF \
    -DUAGENT_P2P_PROFILE:BOOL=OFF \
    --no-warn-unused-cli \
    "-Dmicroxrcedds_agent_DIR:PATH=$ws/install/micro_ros_agent/share/microxrcedds_agent/cmake" \
    "-Dfastrtps_DIR:PATH=$fastdds" \
    "-Dfastcdr_DIR:PATH=$fastcdr" \
    "-Dspdlog_DIR:PATH=$spdlog" \
    -DCMAKE_IGNORE_PREFIX_PATH:STRING=/usr/local \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY:BOOL=OFF \
    -DUAGENT_USE_SYSTEM_LOGGER:BOOL=ON
# Verify shared-library loading and Agent startup before reporting success.
set +u
source "$ws/install/local_setup.bash"
set -u
python3 - "$ws" <<'PY'
from pathlib import Path
import signal, socket, subprocess, sys, time
workspace = Path(sys.argv[1])
executable = workspace / 'install/micro_ros_agent/lib/micro_ros_agent/micro_ros_agent'
log = workspace / 'log/agent_startup.log'
log.parent.mkdir(parents=True, exist_ok=True)
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
    probe.bind(('127.0.0.1', 0))
    port = probe.getsockname()[1]
with log.open('w') as output:
    process = subprocess.Popen([str(executable), 'udp4', '-p', str(port), '-v', '4'],
                               stdout=output, stderr=subprocess.STDOUT)
    try:
        time.sleep(3)
        running = process.poll() is None
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
contents = log.read_text(errors='replace')
print(contents)
if not running or 'running...' not in contents:
    raise SystemExit('Agent startup check failed. Inspect: ' + str(log))
print('Agent startup OK (UDP only; MCU serial communication still needs hardware).')
PY
echo "Agent built. Run: bash $root/Tools/scripts/agent.sh /dev/ttyUSB0 $ROS_BUILD_DISTRO"
