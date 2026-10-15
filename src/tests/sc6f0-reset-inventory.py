#!/usr/bin/env python3
"""Read-only live SC6F0 reset/GPIO ownership inventory; no reset operations."""
import json
from pathlib import Path
import struct


def cells(data):
    return list(struct.unpack('>' + 'I' * (len(data) // 4), data))


def strings(data):
    return data.rstrip(b'\0').decode(errors='replace').split('\0')


def inventory():
    root = Path('/sys/firmware/devicetree/base')
    if not root.is_dir():
        raise RuntimeError('live device tree unavailable')
    providers = {}
    for p in root.rglob('phandle'):
        node = p.parent
        providers[cells(p.read_bytes())[0]] = {
            'node': '/' + str(node.relative_to(root)),
            'compatible': strings((node / 'compatible').read_bytes()) if (node / 'compatible').exists() else [],
            'gpio_cells': cells((node / '#gpio-cells').read_bytes())[0] if (node / '#gpio-cells').exists() else None,
            'reset_cells': cells((node / '#reset-cells').read_bytes())[0] if (node / '#reset-cells').exists() else None,
        }
    drivers = {}
    for dev in Path('/sys/bus/platform/devices').iterdir():
        if (dev / 'of_node').is_dir() and (dev / 'driver').exists():
            drivers[str((dev / 'of_node').resolve())] = (dev / 'driver').resolve().name
    bindings = []
    for node in [root, *[p for p in root.rglob('*') if p.is_dir()]]:
        props = {}
        for p in node.iterdir():
            if not p.is_file():
                continue
            name = p.name
            if name in ('reset-gpios', 'phy-reset-gpios', 'clock-ready-gpios',
                        'packer-reset-gpios', 'video-reset-gpios', 'resets'):
                props[name] = cells(p.read_bytes())
            elif name in ('reset-names', 'status', 'compatible'):
                props[name] = strings(p.read_bytes())
        if not any(k in props for k in ('reset-gpios', 'phy-reset-gpios', 'clock-ready-gpios',
                                       'packer-reset-gpios', 'video-reset-gpios', 'resets')):
            continue
        bindings.append({'node': '/' + str(node.relative_to(root)),
                         'driver': drivers.get(str(node.resolve())), 'properties': props})
    return {'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
            'product': strings((root / 'vp_boot/product').read_bytes())[0],
            'release': Path('/etc/sc6f0-release-id').read_text(),
            'providers': providers, 'bindings': bindings,
            'gpio_claims': Path('/sys/kernel/debug/gpio').read_text()}


if __name__ == '__main__':
    print(json.dumps(inventory(), indent=2))
