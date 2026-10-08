"""Real-ROS worker contract test (domain 0; no Agent/MCU required).

Only invalid MCU commands are issued. Sensor data comes from a labeled host fixture.
"""
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time

TOOLS = Path(__file__).resolve().parents[3]
os.environ['ROS_DOMAIN_ID'] = '0'
os.environ['RMW_IMPLEMENTATION'] = 'rmw_fastrtps_cpp'
os.environ.pop('ROS_LOCALHOST_ONLY', None)
os.environ['ROS_AUTOMATIC_DISCOVERY_RANGE'] = 'LOCALHOST'
os.environ['ROS_STATIC_PEERS'] = '127.0.0.1'
os.environ['FASTRTPS_DEFAULT_PROFILES_FILE'] = str(TOOLS / 'config/fastdds_wsl_local.xml')
os.environ['FASTDDS_DEFAULT_PROFILES_FILE'] = os.environ['FASTRTPS_DEFAULT_PROFILES_FILE']
import rclpy
from sensor_msgs.msg import Temperature


def main():
    rclpy.init()
    node = rclpy.create_node('qt_ros2_worker_contract_fixture')
    pub = node.create_publisher(Temperature, '/qt_ros2_runtime_test/temperature', 10)
    received = queue.Queue()
    checks = []
    errors = TOOLS / 'build/qt_ros2/worker_stderr.log'
    errors.parent.mkdir(parents=True, exist_ok=True)
    report = {'passed': False, 'domain': 0, 'fixture': 'host-generated, no MCU', 'checks': checks}
    with errors.open('w') as err:
        worker = subprocess.Popen(['bash', str(TOOLS / 'runtime/ros2/run_worker.sh'), 'jazzy', '0', '1'],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err,
                                  text=True, bufsize=1)
        def reader():
            for line in worker.stdout:
                try:
                    received.put(json.loads(line))
                except ValueError:
                    pass
        threading.Thread(target=reader, daemon=True).start()
        def wait(predicate, label):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.02)
                while not received.empty():
                    item = received.get_nowait()
                    if predicate(item):
                        return item
                if worker.poll() is not None:
                    raise RuntimeError('Worker exited unexpectedly; see ' + str(errors))
            raise TimeoutError(label)
        def request(ident, op, **values):
            worker.stdin.write(json.dumps({'id': ident, 'op': op, **values}) + '\n')
            worker.stdin.flush()
            return wait(lambda e: e.get('event') == 'response' and e.get('id') == ident, op)
        try:
            wait(lambda e: e.get('event') == 'ready' and e['domain'] == 0, 'ready')
            # Malformed input must not kill the process or prevent later valid commands.
            worker.stdin.write('not-json\n[]\n')
            worker.stdin.flush()
            result = request('graph', 'graph')
            assert result['ok'] and 'topics' in result['result']
            checks.append('malformed-input recovery and ROS graph')
            for ident, op, values in (
                ('int32', 'publish', {'board': 'esp32s3', 'value': 2**31}),
                ('int64', 'service', {'board': 'stm32', 'a': str(2**63), 'b': '0'}),
                ('unknown', 'publish', {'board': 'unknown', 'value': 1}),
                ('type', 'subscribe', {'topic': '/bad', 'type': 'std_msgs/msg/Int32'}),
            ):
                assert not request(ident, op, **values)['ok'], ident
                checks.append(ident + ' rejected without terminating worker')
            result = request('subscribe', 'subscribe', topic='/qt_ros2_runtime_test/temperature',
                             type='sensor_msgs/msg/Temperature')
            assert result['ok']
            report['fixture_domain'] = node.context.get_domain_id()
            report['nodes_before_sensor'] = request('sensor-graph', 'graph')['result']['nodes']
            node.create_timer(0.05, lambda: pub.publish(Temperature(temperature=float('nan'), variance=0.125)))
            sensor = wait(lambda e: e.get('event') == 'sensor', 'sensor preview')
            assert sensor['data']['temperature'] == 'nan' and sensor['data']['variance'] == 0.125
            checks.append('real sensor_msgs subscription and non-finite JSON serialization')
            # EOF alone must clean up the ROS worker; no stale process after Qt closes.
            worker.stdin.close()
            assert worker.wait(timeout=4) == 0
            checks.append('stdin EOF shuts down worker')
            report['passed'] = True
        except Exception as exc:
            report['error'] = str(exc)
        finally:
            if worker.poll() is None:
                worker.terminate()
                worker.wait(timeout=4)
            node.destroy_node()
            rclpy.shutdown()
    destination = errors.parent / 'worker_report.json'
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
