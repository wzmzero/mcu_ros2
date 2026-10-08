"""Test real MCU heartbeat/echo topics and optional STM32 -> ESP32 host relay."""
import argparse
import json
from pathlib import Path
import time

import rclpy
from std_msgs.msg import Int32


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boards', nargs='+', choices=('esp32s3', 'stm32'),
                        default=['esp32s3', 'stm32'])
    parser.add_argument('--relay', action='store_true',
                        help='Relay actual STM32 heartbeats to ESP32 command after echo tests')
    parser.add_argument('--timeout', type=float, default=25)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]
                        / 'build/dual_mcu/topics_validation.json')
    args = parser.parse_args()
    boards = list(dict.fromkeys(args.boards))
    if not 1 <= args.timeout <= 60:
        parser.error('--timeout must be between 1 and 60 seconds')
    if args.relay and set(boards) != {'esp32s3', 'stm32'}:
        parser.error('--relay requires both boards')

    rclpy.init()
    node = rclpy.create_node('mcu_topics_hardware_test')
    state = {board: {'heartbeat': [], 'echo': [], 'command': 57007 + index}
             for index, board in enumerate(boards)}
    publishers = {board: node.create_publisher(Int32, f'/{board}/command', 10)
                  for board in boards}
    relay_sent, relay_echo = [], []
    phase = 'echo'

    def heartbeat(board, message):
        state[board]['heartbeat'].append(message.data)
        print(f'{board} heartbeat: {message.data}', flush=True)
        if phase == 'relay' and board == 'stm32':
            relay_sent.append(message.data)
            publishers['esp32s3'].publish(Int32(data=message.data))

    def echo(board, message):
        state[board]['echo'].append(message.data)
        print(f'{board} echo: {message.data}', flush=True)
        if phase == 'relay' and board == 'esp32s3' and message.data in relay_sent:
            relay_echo.append(message.data)

    subscriptions = []
    for board in boards:
        subscriptions.append(node.create_subscription(Int32, f'/{board}/heartbeat',
            lambda message, board=board: heartbeat(board, message), 10))
        subscriptions.append(node.create_subscription(Int32, f'/{board}/echo',
            lambda message, board=board: echo(board, message), 10))

    def board_passed(board):
        data = state[board]
        return len(set(data['heartbeat'])) >= 3 and data['echo'].count(data['command']) >= 3

    passed = False
    next_send = 0
    deadline = time.monotonic() + args.timeout
    try:
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
            if phase == 'echo' and time.monotonic() >= next_send:
                for board in boards:
                    publishers[board].publish(Int32(data=state[board]['command']))
                next_send = time.monotonic() + 0.25
            if all(board_passed(board) for board in boards):
                if not args.relay:
                    passed = True
                    break
                phase = 'relay'
                if len(set(relay_echo)) >= 3:
                    passed = True
                    break
    finally:
        result = {'time': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'passed': passed,
                  'domain': node.context.get_domain_id(),
                  'boards': {board: dict(state[board], passed=board_passed(board))
                             for board in boards},
                  'relay': {'enabled': args.relay, 'source': '/stm32/heartbeat',
                            'destination': '/esp32s3/command', 'sent': relay_sent,
                            'confirmed_by_esp32_echo': relay_echo,
                            'passed': len(set(relay_echo)) >= 3 if args.relay else None,
                            'mode': 'host_relay', 'firmware_direct_subscription': False}}
        node.destroy_node()
        rclpy.shutdown()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result), flush=True)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
