"""Check real external Agent reuse and reject an unrelated UDP echo server."""
import argparse
import json
from pathlib import Path
import socket
import sys
import time

TOOLS = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(TOOLS / 'runtime/ros2'))
from agent_runtime import AgentRuntime


def wait(runtime, state, timeout=6):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        runtime.tick()
        if runtime.state == state:
            return
        time.sleep(0.02)
    raise TimeoutError(f'Expected {state}, got {runtime.state}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--existing-port', type=int, default=8888)
    args = parser.parse_args()
    checks = []
    runtime = AgentRuntime(None, lambda event: None)
    try:
        runtime.start(args.existing_port)
        wait(runtime, 'running')
        assert not runtime.status()['owned']
        try:
            runtime.stop()
        except RuntimeError:
            pass
        else:
            raise AssertionError('External Agent stop was not rejected')
        runtime.close()
        runtime.start(args.existing_port)
        wait(runtime, 'running')
        checks.append('External Agent reused, stop rejected, still answers after close')
    finally:
        runtime.close()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as echo:
        echo.bind(('127.0.0.1', 0))
        echo.setblocking(False)
        runtime = AgentRuntime(None, lambda event: None)
        try:
            runtime.start(echo.getsockname()[1])
            deadline = time.monotonic() + 6
            while runtime.state != 'error' and time.monotonic() < deadline:
                runtime.tick()
                try:
                    data, address = echo.recvfrom(512)
                    echo.sendto(data, address)
                except BlockingIOError:
                    pass
                time.sleep(0.02)
            assert runtime.state == 'error' and not runtime.status()['verified']
            checks.append('Occupied non-XRCE UDP port is not reported as a running Agent')
        finally:
            runtime.close()
    report = {'passed': True, 'checks': checks, 'existing_port': args.existing_port}
    path = TOOLS / 'build/qt_ros2/agent_reuse_report.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
