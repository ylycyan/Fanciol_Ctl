#!/usr/bin/env python3
"""Internal packer used by the PlatformIO post-build hook."""

import argparse
import zlib
from pathlib import Path

APP_LIMIT = 208 * 1024
APP_ADDRESS = 0x00001000
UPDATER_ADDRESS = 0x0006D000


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def ihex_record(address, record_type, payload=b''):
    body = bytes((len(payload), (address >> 8) & 0xFF, address & 0xFF, record_type)) + payload
    return ':' + (body + bytes((-sum(body) & 0xFF,))).hex().upper()


def write_hex(output, segments):
    lines = []
    active_upper = None
    for name, start, image, limit in segments:
        if not image or len(image) > limit:
            raise SystemExit(f'{name} size {len(image)} exceeds {limit} bytes')
        cursor = 0
        while cursor < len(image):
            absolute = start + cursor
            upper = absolute >> 16
            if upper != active_upper:
                lines.append(ihex_record(0, 4, upper.to_bytes(2, 'big')))
                active_upper = upper
            local = absolute & 0xFFFF
            count = min(16, len(image) - cursor, 0x10000 - local)
            lines.append(ihex_record(local, 0, image[cursor:cursor + count]))
            cursor += count
    lines.append(ihex_record(0, 1))
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n', encoding='ascii')
    print(f'{output}: ' + ', '.join(name for name, *_ in segments))


def package(args):
    """Export addressed HEX components, the complete image and raw OTA BIN."""
    output_dir = Path(args.output_dir)
    jump = ('Boot entry', 0, Path(args.jump).read_bytes(), 4 * 1024)
    app = ('Application', APP_ADDRESS, Path(args.app).read_bytes(), APP_LIMIT)
    updater = ('Updater', UPDATER_ADDRESS, Path(args.updater).read_bytes(), 12 * 1024)
    if not 64 <= len(app[2]) <= APP_LIMIT:
        raise SystemExit(f'application size {len(app[2])} is outside 64..{APP_LIMIT} bytes')
    # Validate all three components before exporting any file.
    write_hex(output_dir / 'sacc26.hex', [jump, app, updater])
    write_hex(output_dir / 'bootload.hex', [jump, updater])
    write_hex(output_dir / 'app.hex', [app])
    (output_dir / 'app.bin').write_bytes(app[2])
    print(f'{output_dir / "app.bin"}: image={len(app[2])} crc={crc32(app[2]):08X}')


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    package_cmd = commands.add_parser('package', help='export the production burn and OTA update firmware files')
    package_cmd.add_argument('--jump', default='BLE/Peripheral/.pio/build/ch583_jump/firmware.bin')
    package_cmd.add_argument('--app', default='BLE/Peripheral/.pio/build/ch583/firmware.bin')
    package_cmd.add_argument('--updater', default='BLE/Peripheral/.pio/build/ch583_updater/firmware.bin')
    package_cmd.add_argument('--output-dir', required=True)
    package_cmd.set_defaults(func=package)

    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
