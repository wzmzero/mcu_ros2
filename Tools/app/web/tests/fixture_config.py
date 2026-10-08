"""Host fixture uses separate ROS names; it never publishes to physical MCU topics."""
import os
from pathlib import Path


def configure():
    profile = Path(__file__).resolve().parents[3] / 'config/fastdds_wsl_local.xml'
    os.environ.update(ROS_DOMAIN_ID='0', RMW_IMPLEMENTATION='rmw_fastrtps_cpp',
                      ROS_AUTOMATIC_DISCOVERY_RANGE='LOCALHOST', ROS_STATIC_PEERS='127.0.0.1',
                      FASTRTPS_DEFAULT_PROFILES_FILE=str(profile), FASTDDS_DEFAULT_PROFILES_FILE=str(profile))
    for key in ('ROS_LOCALHOST_ONLY', 'ROS_DISCOVERY_SERVER', 'ROS_SUPER_CLIENT'):
        os.environ.pop(key, None)
    remappings = ['--ros-args']
    fields = ('command','heartbeat','echo','peer_received','roundtrip','service_result',
              'action_feedback','action_result','action_status','add_two_ints','fibonacci')
    for board in ('esp32s3','stm32'):
        for field in fields:
            original = f'/{board}/{field}'
            remappings += ['-r', f'{original}:=/web_fixture{original}']
    return remappings
