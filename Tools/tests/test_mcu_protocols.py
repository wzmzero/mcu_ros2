"""Real two-MCU ROS 2 topic/service/action tests; no host topic forwarding."""
import argparse
import json
from pathlib import Path
import time

import rclpy
from rclpy.action import ActionClient
from action_msgs.msg import GoalStatus
from example_interfaces.action import Fibonacci
from example_interfaces.srv import AddTwoInts
from std_msgs.msg import Int32, Int64

BOARDS = ('esp32s3', 'stm32')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout', type=float, default=55)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]
                        / 'build/dual_mcu/protocols_validation.json')
    args = parser.parse_args()
    if not 10 <= args.timeout <= 60:
        parser.error('--timeout must be between 10 and 60 seconds')
    rclpy.init()
    node = rclpy.create_node('mcu_protocols_hardware_test')
    report = {'time': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'passed': False,
              'domain': node.context.get_domain_id(), 'host_topic_forwarding': False,
              'boards': {b: {'observations': {}, 'services': [], 'actions': {}} for b in BOARDS}}
    subscriptions = []
    service_clients = {}
    action_clients = {}
    last_seen = {}
    deadline = time.monotonic() + args.timeout

    def received(board, topic, msg):
        last_seen[(board, topic)] = time.monotonic()
        samples = report['boards'][board]['observations'][topic]
        samples.append(msg.data)
        del samples[:-64]

    for board in BOARDS:
        for topic in ('heartbeat', 'peer_received', 'roundtrip', 'service_result',
                      'action_feedback', 'action_result', 'action_status'):
            report['boards'][board]['observations'][topic] = []
            subscriptions.append(node.create_subscription(
                Int64 if topic == 'service_result' else Int32, f'/{board}/{topic}',
                lambda msg, b=board, t=topic: received(b, t, msg), 10))
        service_clients[board] = node.create_client(AddTwoInts, f'/{board}/add_two_ints')
        action_clients[board] = ActionClient(node, Fibonacci, f'/{board}/fibonacci')

    def wait(predicate, label):
        while not predicate():
            if time.monotonic() >= deadline:
                raise TimeoutError(label)
            rclpy.spin_once(node, timeout_sec=0.05)

    def wait_futures(futures, label):
        wait(lambda: all(f.done() for f in futures.values()), label)
        return {b: f.result() for b, f in futures.items()}

    def check(condition, description):
        if not condition:
            raise AssertionError(description)

    def observations(board):
        return report['boards'][board]['observations']

    def native_passed():
        for board, peer in (('esp32s3', 'stm32'), ('stm32', 'esp32s3')):
            data = observations(board)
            confirmed = set(data['heartbeat']) & set(observations(peer)['peer_received']) & set(data['roundtrip'])
            if len(confirmed) < 3 or data['service_result'].count(42) < 2:
                return False
            if not {GoalStatus.STATUS_SUCCEEDED, GoalStatus.STATUS_CANCELED} <= set(data['action_status']):
                return False
            if 5 not in data['action_result'] or not any(n >= 3 for n in data['action_feedback']):
                return False
            if any(time.monotonic() - last_seen.get((board, topic), 0) > 3
                   for topic in ('heartbeat', 'peer_received', 'roundtrip')):
                return False
        return True

    def start_goals(label, order):
        feedback = {b: [] for b in BOARDS}
        futures = {}
        for board in BOARDS:
            report['boards'][board]['actions'][label] = {'order': order, 'feedback': feedback[board]}
            futures[board] = action_clients[board].send_goal_async(
                Fibonacci.Goal(order=order),
                feedback_callback=lambda msg, b=board: feedback[b].append(list(msg.feedback.sequence)))
        handles = wait_futures(futures, f'{label}: goal response')
        for board, handle in handles.items():
            report['boards'][board]['actions'][label]['accepted'] = handle.accepted
        return handles, feedback

    try:
        wait(lambda: all(c.service_is_ready() for c in service_clients.values()) and
             all(c.server_is_ready() for c in action_clients.values()), 'MCU servers discovery')
        print('Both MCU service/action servers discovered', flush=True)
        # Agent discovery can briefly advertise entities from before a board
        # reset. Observe live MCU requests and roundtrips before sending RPCs.
        wait(native_passed, 'Live native MCU topic/service/action paths after startup')
        print('Both MCU native paths active', flush=True)
        cases = [(-100, 58, -42), (2**40, 7, 2**40 + 7),
                 (2**63 - 1, 1, 2**63 - 1), (-2**63, -1, -2**63)]
        for a, b, expected in cases:
            replies = wait_futures({board: c.call_async(AddTwoInts.Request(a=a, b=b))
                                    for board, c in service_clients.items()}, 'AddTwoInts response')
            for board, response in replies.items():
                report['boards'][board]['services'].append(
                    {'a': a, 'b': b, 'sum': response.sum, 'expected': expected})
                check(response.sum == expected, f'{board}: AddTwoInts({a}, {b})')
        print('Both MCU service servers: signed 64-bit and saturation passed', flush=True)

        handles, feedback = start_goals('success', 6)
        check(all(h.accepted for h in handles.values()), 'Valid Fibonacci goal rejected')
        results = wait_futures({b: h.get_result_async() for b, h in handles.items()}, 'Fibonacci result')
        for board, result in results.items():
            report['boards'][board]['actions']['success'].update(
                status=result.status, sequence=list(result.result.sequence))
            check(result.status == GoalStatus.STATUS_SUCCEEDED and
                  list(result.result.sequence) == [0, 1, 1, 2, 3, 5] and feedback[board],
                  f'{board}: Fibonacci success/feedback')
        print('Both MCU action servers: feedback and result passed', flush=True)

        handles, _ = start_goals('reject', 11)
        check(all(not h.accepted for h in handles.values()), 'Out-of-range goal accepted')
        # Allow rclc to release the rejected server handles before the next goal.
        for _ in range(4):
            rclpy.spin_once(node, timeout_sec=0.05)
        handles, feedback = start_goals('cancel', 10)
        check(all(h.accepted for h in handles.values()), 'Cancellation test goal rejected')
        result_futures = {b: h.get_result_async() for b, h in handles.items()}
        wait(lambda: all(feedback[b] for b in BOARDS), 'Feedback before cancel')
        cancellations = wait_futures({b: h.cancel_goal_async() for b, h in handles.items()}, 'Cancel acknowledgment')
        results = wait_futures(result_futures, 'Canceled goal result')
        for board, result in results.items():
            report['boards'][board]['actions']['cancel'].update(
                cancel_acknowledged=len(cancellations[board].goals_canceling) == 1,
                status=result.status, sequence=list(result.result.sequence))
            check(len(cancellations[board].goals_canceling) == 1 and
                  result.status == GoalStatus.STATUS_CANCELED and
                  3 <= len(result.result.sequence) < 10, f'{board}: Fibonacci cancellation')
        print('Both MCU action servers: rejection and cancellation passed', flush=True)

        wait(native_passed, 'Native MCU topic roundtrip/service/action clients')
        for board, peer in (('esp32s3', 'stm32'), ('stm32', 'esp32s3')):
            data = observations(board)
            report['boards'][board]['native_roundtrip_confirmed'] = sorted(
                set(data['heartbeat']) & set(observations(peer)['peer_received']) & set(data['roundtrip']))
            report['boards'][board]['native_service_client_passed'] = data['service_result'].count(42) >= 2
            report['boards'][board]['native_action_client_passed'] = True
        report['passed'] = True
        print('Native two-MCU topic roundtrip, service and action clients passed', flush=True)
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
    finally:
        for client in action_clients.values():
            client.destroy()
        node.destroy_node()
        rclpy.shutdown()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report), flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
