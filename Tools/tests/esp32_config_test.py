"""Check all transport choices with ESP-IDF's real Kconfig tree, without changing sdkconfig."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2] / 'esp32s3_ros2'
SDK = Path(os.environ['IDF_PATH'])
BUILD = ROOT / 'build'
original = (ROOT / 'sdkconfig').read_bytes()
template = original.decode('utf-8')
transports = ('USB_SERIAL_JTAG', 'UART1', 'WIFI_UDP', 'USB_RNDIS_UDP')

with tempfile.TemporaryDirectory(prefix='config-test-', dir=BUILD) as directory:
    folder = Path(directory)
    config = folder / 'sdkconfig'
    header = folder / 'sdkconfig.h'
    for transport in transports:
        # Start with conflicting prior TinyUSB choices to exercise dependency correction.
        content = re.sub(r'^(?:# )?CONFIG_APP_ROS_(?:' + '|'.join(transports) + r')[= ].*\n',
                         '', template, flags=re.MULTILINE)
        content = re.sub(r'^(?:# )?CONFIG_TINYUSB_(?:NET_MODE_\w+|CDC_ENABLED)[= ].*\n',
                         '', content, flags=re.MULTILINE)
        content += f'\nCONFIG_APP_ROS_{transport}=y\nCONFIG_TINYUSB_NET_MODE_NCM=y\nCONFIG_TINYUSB_CDC_ENABLED=y\n'
        config.write_text(content, encoding='utf-8')
        command = [sys.executable, '-m', 'kconfgen', '--list-separator=semicolon',
                   '--kconfig', str(SDK / 'Kconfig'), '--config', str(config),
                   '--env-file', str(BUILD / 'config.env'), '--env', 'IDF_TARGET=esp32s3',
                   '--env', 'IDF_TOOLCHAIN=gcc', '--env', 'IDF_MINIMAL_BUILD=n',
                   '--env', f'COMPONENT_KCONFIGS_SOURCE_FILE={BUILD / "kconfigs.in"}',
                   '--env', f'COMPONENT_KCONFIGS_PROJBUILD_SOURCE_FILE={BUILD / "kconfigs_projbuild.in"}',
                   '--output', 'header', str(header), '--output', 'config', str(config)]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
        assert result.returncode == 0, result.stdout + result.stderr
        values = header.read_text()
        for item in transports:
            assert (f'#define CONFIG_APP_ROS_{item} 1' in values) == (item == transport)
        if transport == 'USB_RNDIS_UDP':
            assert '#define CONFIG_TINYUSB_NET_MODE_ECM_RNDIS 1' in values
            assert '#define CONFIG_TINYUSB_NET_MODE_NCM 1' not in values
            assert '#define CONFIG_TINYUSB_CDC_ENABLED 1' not in values
            assert '#define CONFIG_LWIP_DHCPS 1' in values
        else:
            assert '#define CONFIG_TINYUSB_NET_MODE_ECM_RNDIS 1' not in values
        print(f'ESP-IDF configuration: {transport} passed', flush=True)

assert (ROOT / 'sdkconfig').read_bytes() == original, 'The test changed the active configuration'
