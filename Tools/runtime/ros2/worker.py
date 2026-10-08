"""ROS 2 runtime for the Qt host. JSON-lines stdin/stdout; diagnostics on stderr.

All ROS callbacks and commands run on one executor thread. The stdin thread only
queues commands. This process never forwards MCU topics between boards.
"""
import json
import queue
import sys
import threading
import time
import uuid
from pathlib import Path

import rclpy
from rclpy.action import ActionClient
from rclpy.qos import qos_profile_sensor_data
from rclpy.executors import ExternalShutdownException
from example_interfaces.action import Fibonacci
from example_interfaces.srv import AddTwoInts
from std_msgs.msg import Int32, Int64
from rosidl_runtime_py.utilities import get_message
from rosidl_runtime_py.convert import message_to_ordereddict
from ament_index_python.packages import get_package_prefix, PackageNotFoundError
from agent_runtime import AgentRuntime

BOARDS = ('esp32s3', 'stm32')
FIELDS = ('heartbeat', 'echo', 'peer_received', 'roundtrip', 'service_result',
          'action_feedback', 'action_result', 'action_status')


def emit(data):
    # Keep a malformed/non-finite sensor value from breaking the JSON protocol.
    sys.stdout.write(json.dumps(data, ensure_ascii=True, allow_nan=False) + '\n')
    sys.stdout.flush()


class Worker:
    def __init__(self):
        self.node = rclpy.create_node('qt_ros2_' + uuid.uuid4().hex[:8])
        self.commands = queue.Queue(maxsize=256)
        self.eof = threading.Event()
        self.subscriptions = []
        self.publishers = {}
        self.services = {}
        self.actions = {}
        self.pending = {}
        self.goals = {}
        self.sensor = None
        self.latest_sensors = {}
        self.last_sensor_flush = 0.0
        self.running = True
        try:
            executable = Path(get_package_prefix('micro_ros_agent')) / 'lib/micro_ros_agent/micro_ros_agent'
            if not executable.is_file():
                executable = None
        except PackageNotFoundError:
            executable = None
        self.agent = AgentRuntime(executable, emit)
        for board in BOARDS:
            self.publishers[board] = self.node.create_publisher(Int32, f'/{board}/command', 10)
            self.services[board] = self.node.create_client(AddTwoInts, f'/{board}/add_two_ints')
            self.actions[board] = ActionClient(self.node, Fibonacci, f'/{board}/fibonacci')
            for field in FIELDS:
                self.subscriptions.append(self.node.create_subscription(
                    Int64 if field == 'service_result' else Int32, f'/{board}/{field}',
                    lambda msg, b=board, f=field: emit({'event': 'telemetry', 'board': b,
                                                     'field': f, 'value': str(msg.data)}), 10))
        threading.Thread(target=self.read_commands, daemon=True).start()

    def read_commands(self):
        try:
            for line in sys.stdin:
                try:
                    command = json.loads(line)
                    if not isinstance(command, dict):
                        raise ValueError('Expected a JSON object')
                    self.commands.put(command, timeout=1)
                except (ValueError, queue.Full) as exc:
                    print(f'Invalid command: {exc}', file=sys.stderr, flush=True)
        finally:
            self.eof.set()

    def response(self, command, result=None, error=None):
        emit({'event': 'response', 'id': command.get('id', ''),
              'op': command.get('op', ''), 'ok': error is None,
              'board': command.get('board', ''),
              **({'error': str(error)} if error is not None else {'result': result})})

    def finish(self, ident, result=None, error=None):
        item = self.pending.pop(ident, None)
        self.goals.pop(ident, None)
        if item:
            self.response(item['command'], result, error)

    def defer(self, command, timeout=12):
        ident = command['id']
        if ident in self.pending:
            raise ValueError('Request ID already active')
        self.pending[ident] = {'command': command, 'deadline': time.monotonic() + timeout}
        return ident

    def service_done(self, ident, future):
        try:
            self.finish(ident, {'sum': str(future.result().sum)})
        except Exception as exc:
            self.finish(ident, error=exc)

    def goal_done(self, ident, future):
        try:
            goal = future.result()
            if ident not in self.pending:
                if goal.accepted:
                    goal.cancel_goal_async()
                return
            if not goal.accepted:
                self.finish(ident, {'accepted': False, 'status': 0, 'sequence': []})
                return
            self.goals[ident] = goal
            emit({'event': 'action_accepted', 'id': ident})
            goal.get_result_async().add_done_callback(lambda f: self.result_done(ident, f))
        except Exception as exc:
            self.finish(ident, error=exc)

    def result_done(self, ident, future):
        try:
            result = future.result()
            self.finish(ident, {'accepted': True, 'status': result.status,
                                'sequence': list(result.result.sequence)})
        except Exception as exc:
            self.finish(ident, error=exc)

    def feedback(self, ident, message):
        if ident in self.pending:
            emit({'event': 'action_feedback', 'id': ident,
                  'sequence': list(message.feedback.sequence)})

    def sensor_received(self, topic, typename, message):
        # Coalesce high-rate messages to 10 Hz; binary/image previews are bounded.
        try:
            def bounded(value):
                if isinstance(value, dict):
                    return {k: bounded(v) for k, v in value.items()}
                if isinstance(value, (list, tuple)):
                    if len(value) > 64:
                        return {'length': len(value), 'preview': [bounded(v) for v in value[:64]]}
                    return [bounded(v) for v in value]
                if isinstance(value, float) and not (-float('inf') < value < float('inf')):
                    return str(value)
                if isinstance(value, int) and abs(value) > 2**53:
                    return str(value)
                return value
            self.latest_sensors[topic] = {'event': 'sensor', 'topic': topic, 'type': typename,
                                          'data': bounded(message_to_ordereddict(message))}
        except Exception as exc:
            print(f'Sensor decode failed: {exc}', file=sys.stderr, flush=True)

    def command(self, c):
        try:
            if not isinstance(c.get('id'), str) or not c['id']:
                raise ValueError('Request ID required')
            op = c.get('op')
            if op == 'stop':
                self.running = False
                self.response(c, {'stopped': True})
                return
            if op in ('agent_start', 'agent_stop', 'agent_status'):
                result = (self.agent.start(c.get('port', 8888)) if op == 'agent_start'
                          else self.agent.stop() if op == 'agent_stop' else self.agent.status())
                if op == 'agent_status':
                    self.agent.notify()
                self.response(c, result)
                return
            if op == 'graph':
                topics = [{'name': name, 'types': types} for name, types in self.node.get_topic_names_and_types()]
                self.response(c, {'topics': topics, 'nodes': self.node.get_node_names(),
                                  'services': self.node.get_service_names_and_types()})
                return
            if op == 'subscribe':
                topic, typename = c['topic'], c['type']
                if not typename.startswith('sensor_msgs/msg/'):
                    raise ValueError('Select a standard sensor_msgs topic')
                cls = get_message(typename)
                # Construct first; an invalid selection leaves the old subscription intact.
                new = self.node.create_subscription(cls, topic,
                    lambda msg: self.sensor_received(topic, typename, msg), qos_profile_sensor_data)
                if self.sensor is not None:
                    self.node.destroy_subscription(self.sensor)
                self.sensor = new
                self.latest_sensors.clear()
                self.response(c, {'topic': topic, 'type': typename})
                return
            board = c.get('board')
            if board not in BOARDS:
                raise ValueError('Unknown board')
            if op == 'publish':
                value = int(c['value'])
                if not -(2**31) <= value < 2**31:
                    raise ValueError('Int32 out of range')
                self.publishers[board].publish(Int32(data=value))
                self.response(c, {'published': value})
            elif op == 'service':
                a, b = int(c['a']), int(c['b'])
                if any(not -(2**63) <= n < 2**63 for n in (a, b)):
                    raise ValueError('AddTwoInts requires signed 64-bit integers')
                if not self.services[board].service_is_ready():
                    raise RuntimeError('Service not discovered; check board and Agent')
                ident = self.defer(c)
                future = self.services[board].call_async(AddTwoInts.Request(a=a, b=b))
                self.pending[ident]['service_future'] = (self.services[board], future)
                future.add_done_callback(lambda f: self.service_done(ident, f))
            elif op == 'action':
                order = int(c['order'])
                if not -(2**31) <= order < 2**31:
                    raise ValueError('Order must fit Int32')
                if any(item['command'].get('board') == board and item['command']['op'] == 'action'
                       for item in self.pending.values()):
                    raise RuntimeError('This board already has a pending goal')
                if not self.actions[board].server_is_ready():
                    raise RuntimeError('Action server not discovered')
                ident = self.defer(c)
                self.actions[board].send_goal_async(Fibonacci.Goal(order=order),
                    feedback_callback=lambda msg: self.feedback(ident, msg)).add_done_callback(
                        lambda f: self.goal_done(ident, f))
            elif op == 'cancel':
                goal = self.goals.get(c['goal_id'])
                if goal is None:
                    raise RuntimeError('No accepted goal to cancel')
                ident = self.defer(c)
                def canceled(future):
                    try:
                        self.finish(ident, {'cancel_requested': bool(future.result().goals_canceling)})
                    except Exception as exc:
                        self.finish(ident, error=exc)
                goal.cancel_goal_async().add_done_callback(canceled)
            else:
                raise ValueError('Unknown operation')
        except Exception as exc:
            if c.get('id') in self.pending:
                self.finish(c['id'], error=exc)
            else:
                self.response(c, error=exc)

    def run(self):
        emit({'event': 'ready', 'domain': self.node.context.get_domain_id(), 'boards': list(BOARDS)})
        try:
            while self.running and not self.eof.is_set() and rclpy.ok():
                for _ in range(32):
                    try:
                        self.command(self.commands.get_nowait())
                    except queue.Empty:
                        break
                rclpy.spin_once(self.node, timeout_sec=0.02)
                self.agent.tick()
                now = time.monotonic()
                if now - self.last_sensor_flush >= 0.1:
                    for value in self.latest_sensors.values():
                        emit(value)
                    self.latest_sensors.clear()
                    self.last_sensor_flush = now
                for ident, item in list(self.pending.items()):
                    if now > item['deadline']:
                        if ident in self.goals:
                            self.goals[ident].cancel_goal_async()
                        if 'service_future' in item:
                            client, future = item['service_future']
                            client.remove_pending_request(future)
                        self.finish(ident, error='ROS request timed out (12 seconds)')
        finally:
            try:
                # Cancel this UI's goals while its Agent is still available.
                if rclpy.ok():
                    for goal in list(self.goals.values()):
                        goal.cancel_goal_async()
                    deadline = time.monotonic() + 0.4
                    while self.goals and rclpy.ok() and time.monotonic() < deadline:
                        rclpy.spin_once(self.node, timeout_sec=0.02)
                for action in self.actions.values():
                    action.destroy()
                self.node.destroy_node()
            finally:
                # Also runs if ROS cleanup fails or stdout has disconnected.
                self.agent.close()


def main():
    rclpy.init()
    try:
        Worker().run()
    except (BrokenPipeError, KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
