"""Explicit host-generated sensor fixture; never represents a physical MCU sensor.

Run in the same ROS domain/profile as Qt; quits after 60 seconds or Ctrl+C.
"""
import time
import rclpy
from sensor_msgs.msg import Temperature


def main():
    rclpy.init()
    node = rclpy.create_node('qt_ros2_test_temperature_fixture')
    publisher = node.create_publisher(Temperature, '/qt_ros2_test/temperature', 10)
    deadline = time.monotonic() + 60
    try:
        while rclpy.ok() and time.monotonic() < deadline:
            msg = Temperature()
            msg.header.stamp = node.get_clock().now().to_msg()
            msg.header.frame_id = 'host_test_fixture'
            msg.temperature = 25.5
            msg.variance = 0.125
            publisher.publish(msg)
            rclpy.spin_once(node, timeout_sec=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
