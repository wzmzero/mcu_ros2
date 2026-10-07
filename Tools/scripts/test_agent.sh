#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/Tools/scripts/ros_env.sh"
resolve_host_root "$root"
load_ros_env "${1:-}"
ws="$HOST_ROOT/build/micro_ros/$ROS_BUILD_DISTRO/agent"
[[ -f "$ws/install/local_setup.bash" ]] || {
    echo "Build the Agent first: bash Tools/scripts/build_agent.sh $ROS_BUILD_DISTRO" >&2
    exit 2
}
set +u
source "$ws/install/local_setup.bash"
set -u
python3 - "$ws" "$root" <<'PY'
from pathlib import Path
import os
import pty
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

workspace = Path(sys.argv[1])
root = Path(sys.argv[2])
executable = workspace / 'install/micro_ros_agent/lib/micro_ros_agent/micro_ros_agent'
logs = workspace / 'log'
logs.mkdir(parents=True, exist_ok=True)
libraries = subprocess.run(['ldd', str(executable)], check=True,
                           capture_output=True, text=True).stdout
(logs / 'agent_libraries.log').write_text(libraries)
if 'not found' in libraries:
    raise SystemExit('Missing shared library; inspect agent_libraries.log')
expected = str(workspace / 'install/micro_ros_agent/lib/libmicroxrcedds_agent.so')
if expected not in libraries:
    raise SystemExit('Agent loaded an XRCE library outside this workspace')
print('Shared libraries OK (workspace XRCE Agent).')

def check_agent(arguments, log_name, probe=None):
    log = logs / log_name
    with log.open('w') as output:
        process = subprocess.Popen([str(executable), *arguments],
                                   stdout=output, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError('Agent exited during startup')
                if 'running...' in log.read_text(errors='replace'):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError('Agent startup timed out')
            if probe:
                probe()
            if process.poll() is not None:
                raise RuntimeError('Agent exited during the test')
        except Exception:
            print(log.read_text(errors='replace'), file=sys.stderr)
            raise
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
    reservation.bind(('127.0.0.1', 0))
    port = reservation.getsockname()[1]

def ping():
    # Same GET_INFO/INFO_ACTIVITY request as uxr_ping_agent_attempts().
    request = bytes.fromhex('80 00 00 00 02 01 08 00 00 0a ff fd 02 00 00 00')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(3)
        client.connect(('127.0.0.1', port))
        for _ in range(3):
            client.send(request)
            response = client.recv(1024)
            if (len(response) < 14 or response[0] != 0x80 or response[4] != 6
                    or response[8:12] != request[8:12] or response[12] != 0):
                raise RuntimeError('Invalid XRCE pong: ' + response.hex())
            length = struct.unpack_from('<H', response, 6)[0]
            if length != len(response) - 8:
                raise RuntimeError('Invalid XRCE pong length')
    print('UDP XRCE ping OK (3 request/reply round trips).')

check_agent(['udp4', '-p', str(port), '-v', '4'], 'agent_udp_test.log', ping)
with tempfile.TemporaryDirectory(prefix='micro_ros_udp_') as temporary:
    callbacks_test = Path(temporary) / 'udp_transport_test'
    subprocess.run(['gcc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'firmware/core'), '-I' + str(root / 'firmware/transport'),
                    str(root / 'Tools/tests/udp_transport_test.c'),
                    str(root / 'firmware/transport/udp_transport.c'), '-o', str(callbacks_test)], check=True)
    check_agent(['udp4', '-p', str(port), '-v', '4'], 'agent_udp_callbacks_test.log',
                lambda: subprocess.run([str(callbacks_test), str(port)], check=True))
master, slave = pty.openpty()
try:
    check_agent(['serial', '--dev', os.ttyname(slave), '-b', '115200', '-v', '4'],
                'agent_serial_test.log')
    print('Serial startup OK (PTY, 115200 baud).')
finally:
    os.close(master)
    os.close(slave)
print('Agent software tests passed. MCU communication still requires hardware.')
PY
