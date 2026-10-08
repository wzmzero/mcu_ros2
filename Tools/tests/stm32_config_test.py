"""Exercise STM32 Kconfig export and invalid inputs without changing board settings."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / 'stm32f4_ros2/scripts/menuconfig.py'

with tempfile.TemporaryDirectory(prefix='stm32-kconfig-') as directory:
    folder = Path(directory)
    config = folder / 'sdkconfig'
    output = folder / 'generated'

    def run(*settings, success=True):
        command = [sys.executable, str(TOOL), '--config', str(config), '--output', str(output)]
        for setting in settings:
            command.extend(['--set', setting])
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
        assert (result.returncode == 0) == success, result.stdout + result.stderr

    run()
    assert 'CONFIG_ROS_TRANSPORT_USB_RNDIS [=[OFF]=]' in (output / 'sdkconfig.cmake').read_text()
    assert '#define CONFIG_ROS_TRANSPORT_UART 1' in (output / 'sdkconfig.h').read_text()
    run('ROS_TRANSPORT_USB_RNDIS=y')
    header = (output / 'sdkconfig.h').read_text()
    assert '#define CONFIG_USB_MCU_IP "192.168.7.3"' in header
    assert '#define CONFIG_USB_HOST_IP "192.168.7.4"' in header
    assert '#define CONFIG_ROS_AGENT_IP "192.168.7.4"' in header
    run('ROS_TRANSPORT_USB_RNDIS=y', 'USB_MCU_IP=192.168.8.10',
        'USB_HOST_IP=192.168.8.20', 'ROS_AGENT_IP=192.168.8.20', 'ROS_AGENT_PORT=9999')
    header = (output / 'sdkconfig.h').read_text()
    assert '#define CONFIG_ROS_TRANSPORT_USB_RNDIS 1' in header
    assert '#define CONFIG_ROS_AGENT_PORT 9999' in header
    assert 'CONFIG_ROS_TRANSPORT_USB_RNDIS [=[ON]=]' in (output / 'sdkconfig.cmake').read_text()
    saved = config.read_bytes()
    for invalid in ('ROS_DOMAIN_ID=233', 'ROS_AGENT_PORT=0', 'USB_HSE_HZ=8100000',
                    'USB_MCU_IP=192.168.9.10', 'USB_HOST_IP=192.168.8.10',
                    'ROS_AGENT_IP=192.168.8.300', 'ROS_AGENT_IP=224.0.0.1',
                    'ROS_AGENT_IP=1.2.3.4]=]message(FATAL_ERROR injected)'):
        run(invalid, success=False)
        assert config.read_bytes() == saved, 'An invalid value overwrote the valid configuration'
    run('ROS_TRANSPORT_UART=y', 'ROS_UART_BAUD=230400', 'ROS_DOMAIN_ID=2')
    cmake = (output / 'sdkconfig.cmake').read_text()
    assert 'CONFIG_ROS_TRANSPORT_USB_RNDIS [=[OFF]=]' in cmake
    assert 'CONFIG_ROS_UART_BAUD [=[230400]=]' in cmake
    assert 'CONFIG_USB_MCU_IP' not in cmake
    run('ROS_UART_BAUD=1', success=False)
    before = {path.name: path.stat().st_mtime_ns for path in output.iterdir()}
    run()
    assert before == {path.name: path.stat().st_mtime_ns for path in output.iterdir()}

print('STM32 Kconfig export, transport switching and invalid input checks passed')
