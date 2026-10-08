"""Host-generated ROS fixture under /web_fixture; never represents MCU hardware."""
import time
import signal
import rclpy
from rclpy.action import ActionServer, CancelResponse
from rclpy.executors import MultiThreadedExecutor
from rclpy.executors import ExternalShutdownException
from rclpy.callback_groups import ReentrantCallbackGroup
from example_interfaces.srv import AddTwoInts
from example_interfaces.action import Fibonacci
from sensor_msgs.msg import Temperature
from std_msgs.msg import Int32


def main():
    rclpy.init()
    signal.signal(signal.SIGTERM, lambda *_: rclpy.try_shutdown())
    node = rclpy.create_node('web_host_fixture')
    group = ReentrantCallbackGroup()
    pubs = [node.create_publisher(Temperature, f'/web_fixture/temperature{i}', 10) for i in (1, 2)]
    heartbeats = [node.create_publisher(Int32, f'/web_fixture/{board}/heartbeat', 10) for board in ('esp32s3', 'stm32')]
    echo = node.create_publisher(Int32, '/web_fixture/esp32s3/echo', 10)
    node.create_subscription(Int32, '/web_fixture/esp32s3/command', lambda msg: echo.publish(msg), 10)
    sequence = 0

    def tick():
        nonlocal sequence
        sequence += 1
        for i, pub in enumerate(pubs):
            pub.publish(Temperature(temperature=25.5 + i, variance=0.125))
        for pub in heartbeats:
            pub.publish(Int32(data=sequence))

    node.create_timer(0.05, tick)

    def add(request, response):
        response.sum = request.a + request.b
        return response

    node.create_service(AddTwoInts, '/web_fixture/stm32/add_two_ints', add, callback_group=group)

    def execute(goal):
        values = [0, 1]
        for _ in range(max(0, goal.request.order - 2)):
            time.sleep(0.08)
            if goal.is_cancel_requested:
                goal.canceled()
                return Fibonacci.Result(sequence=values)
            values.append(values[-1] + values[-2])
            goal.publish_feedback(Fibonacci.Feedback(sequence=values))
        goal.succeed()
        return Fibonacci.Result(sequence=values)

    action = ActionServer(node, Fibonacci, '/web_fixture/stm32/fibonacci', execute,
                          cancel_callback=lambda goal: CancelResponse.ACCEPT,
                          callback_group=group)
    executor = MultiThreadedExecutor(num_threads=4)
    executor.add_node(node)
    print(f'Host fixture ready: Domain {node.context.get_domain_id()}', flush=True)
    try:
        executor.spin()
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        executor.shutdown()
        action.destroy()
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
