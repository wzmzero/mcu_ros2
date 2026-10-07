"""Kconfig frontend for native Windows/Linux CMake builds; dependencies stay local."""
import argparse
import importlib
import ipaddress
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / '.tools/menuconfig'
sys.path.insert(0, str(PACKAGES))

def dependency(module, package):
    try:
        return importlib.import_module(module)
    except ImportError:
        print(f'Installing {package} in {PACKAGES}', flush=True)
        subprocess.run([sys.executable, '-m', 'pip', 'install', '--disable-pip-version-check',
                        '--no-warn-script-location', '--target', str(PACKAGES), package], check=True)
        importlib.invalidate_caches()
        return importlib.import_module(module)

def validate(config):
    """Reject values that Kconfig cannot constrain before exporting CMake code."""
    if config.syms['ROS_TRANSPORT_USB_RNDIS'].str_value != 'y':
        return
    if int(config.syms['USB_HSE_HZ'].str_value) % 1000000:
        raise ValueError('USB_HSE_HZ must be a whole MHz frequency')
    addresses = {}
    for name in ('USB_MCU_IP', 'USB_HOST_IP', 'ROS_AGENT_IP'):
        address = ipaddress.IPv4Address(config.syms[name].str_value)
        if address.is_multicast or int(address) in (0, 0xffffffff):
            raise ValueError(f'{name} must be an individual IPv4 address')
        addresses[name] = address
    mcu, host = addresses['USB_MCU_IP'], addresses['USB_HOST_IP']
    if (int(mcu) >> 8 != int(host) >> 8 or mcu == host or
            int(mcu) & 255 in (0, 255) or int(host) & 255 in (0, 255)):
        raise ValueError('USB MCU and host must use distinct host addresses in the same /24')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=ROOT / 'sdkconfig')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--menuconfig', action='store_true')
    parser.add_argument('--seed', action='append', default=[])
    parser.add_argument('--set', action='append', default=[], dest='settings')
    args = parser.parse_args()
    os.environ['KCONFIG_CONFIG'] = str(args.config.resolve())
    os.environ['srctree'] = str(ROOT)
    kconfiglib = dependency('kconfiglib', 'kconfiglib==14.1.0')
    config = kconfiglib.Kconfig(str(ROOT / 'Kconfig'), warn_to_stderr=True)
    existed = args.config.is_file()
    if existed:
        config.load_config(str(args.config))
    for item in (args.seed if not existed else []) + args.settings:
        name, value = item.split('=', 1)
        if name not in config.syms or not config.syms[name].set_value(value):
            raise ValueError(f'Invalid Kconfig setting: {name}')
        sym = config.syms[name]
        if sym.visibility and sym.str_value != value:
            raise ValueError(f'{name}={value} is outside the allowed Kconfig range')
    if args.menuconfig:
        if os.name == 'nt':
            dependency('_curses', 'windows-curses>=2.3,<3')
        if not sys.stdin.isatty() or not sys.stdout.isatty():
            raise RuntimeError('menuconfig needs an interactive terminal. Run it in PowerShell or WSL.')
        from menuconfig import menuconfig
        menuconfig(config)
    else:
        validate(config)
        args.config.parent.mkdir(parents=True, exist_ok=True)
        config.write_config(str(args.config))
    if args.output:
        validate(config)
        args.output.mkdir(parents=True, exist_ok=True)
        config.write_autoconf(str(args.output / 'sdkconfig.h'))
        mappings = {
            'STM32_USB_RNDIS': 'ROS_TRANSPORT_USB_RNDIS', 'ROS_DOMAIN_ID': 'ROS_DOMAIN_ID',
            'STM32_ROS_UART_BAUD': 'ROS_UART_BAUD', 'STM32_USB_HSE_HZ': 'USB_HSE_HZ',
            'STM32_USB_MCU_IP': 'USB_MCU_IP', 'STM32_USB_HOST_IP': 'USB_HOST_IP',
            'STM32_ROS_AGENT_IP': 'ROS_AGENT_IP', 'STM32_ROS_AGENT_PORT': 'ROS_AGENT_PORT',
        }
        entries = []
        for name, symbol in mappings.items():
            sym = config.syms[symbol]
            if not sym.visibility:
                continue
            value = sym.str_value
            if sym.type == kconfiglib.BOOL:
                value = 'ON' if value == 'y' else 'OFF'
            # Export only validated integers, booleans and numeric IPv4 strings.
            entries.append(f'set({name} [=[{value}]=] CACHE STRING "From STM32 sdkconfig" FORCE)')
        path = args.output / 'sdkconfig.cmake'
        content = '\n'.join(entries) + '\n'
        if not path.exists() or path.read_text(encoding='utf-8') != content:
            path.write_text(content, encoding='utf-8')
        print(f'Configuration: {args.config}; generated headers: {args.output}')

if __name__ == '__main__':
    main()
