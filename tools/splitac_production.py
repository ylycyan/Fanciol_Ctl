#!/usr/bin/env python3
"""SplitAC factory image, raw BIN OTA and configuration tool."""

import argparse
import csv
import re
import struct
import zlib
from pathlib import Path

APP_LIMIT = 208 * 1024
APP_ADDRESS = 0x00001000
UPDATER_ADDRESS = 0x0006D000
CONNECTIVITY_MAGIC = 0x3154454E
CONNECTIVITY_STATE_ACTIVE = 0xA1
CONNECTIVITY_SCHEMA = 4
CONFIG_MAGIC = 0x31474643
CONFIG_SCHEMA = 4
DATAFLASH_SIZE = 32 * 1024
DEVICE_ID_PATTERN = re.compile(r'^A\d{2}(?:0[1-9]|1[0-2])(?!0000)[0-9A-F]{4}$')
COMMUNICATION_MODES = {'lora': 1, '4g': 2, 'both': 3}


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_version(text):
    parts = text.split('.')
    if len(parts) not in (2, 3) or any(not p.isdigit() for p in parts):
        raise argparse.ArgumentTypeError('version must be MAJOR.MINOR[.PATCH]')
    values = [int(p) for p in parts] + [0] * (3 - len(parts))
    if any(value > 255 for value in values):
        raise argparse.ArgumentTypeError('version component exceeds 255')
    return (values[0] << 16) | (values[1] << 8) | values[2]


def ota(args):
    image = Path(args.app).read_bytes()
    if not 64 <= len(image) <= APP_LIMIT:
        raise SystemExit(f'application size {len(image)} is outside 64..{APP_LIMIT} bytes')
    output = Path(args.output)
    if output.suffix.lower() != '.bin':
        raise SystemExit('OTA output must use the .bin extension')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    print(f'{output}: image={len(image)} crc={crc32(image):08X}')


def ihex_record(address, record_type, payload=b''):
    body = bytes((len(payload), (address >> 8) & 0xFF, address & 0xFF, record_type)) + payload
    return ':' + (body + bytes((-sum(body) & 0xFF,))).hex().upper()


def factory(args):
    segments = [
        ('Boot entry', 0, Path(args.jump).read_bytes(), 4 * 1024),
        ('Application', APP_ADDRESS, Path(args.app).read_bytes(), APP_LIMIT),
        ('Updater', UPDATER_ADDRESS, Path(args.updater).read_bytes(), 12 * 1024),
    ]
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
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n', encoding='ascii')
    print(f'{output}: complete image with Boot entry, application and Updater')


def package(args):
    """Export the only two firmware files exposed to production and service."""
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    factory(argparse.Namespace(
        jump=args.jump, app=args.app, updater=args.updater,
        output=str(output_dir / 'splitac-burn.hex')
    ))
    ota(argparse.Namespace(
        app=args.app, output=str(output_dir / 'splitac-update.bin'), version=None
    ))


def fixed_text(value, size, field):
    encoded = value.encode('ascii')
    if len(encoded) >= size or any(ch in value for ch in ('"', '\\')):
        raise ValueError(f'{field} must be ASCII and shorter than {size} bytes')
    return encoded + b'\0' * (size - len(encoded))


def connectivity_record(row, args):
    communication_mode = row['communication_mode'].strip().lower()
    transport = COMMUNICATION_MODES.get(communication_mode)
    if transport is None:
        raise ValueError('communication_mode is invalid')
    payload = struct.pack(
        '<BBBBBBBBHHHH48s20s10s',
        transport, args.register_sf, args.register_bw,
        args.listen_sf, args.listen_bw, args.pdp_type, args.qos,
        1 if args.clean_session else 0, args.port, args.keepalive,
        args.report_interval, args.network_timeout,
        fixed_text(args.broker, 48, 'broker'),
        fixed_text(row.get('apn') or args.apn, 20, 'apn'),
        fixed_text(row['device_id'], 10, 'device_id')
    )
    prefix = struct.pack('<IBBIH', CONNECTIVITY_MAGIC, CONNECTIVITY_SCHEMA,
                         CONNECTIVITY_STATE_ACTIVE, 1, len(payload))
    checksum = crc32(prefix + payload)
    record = prefix + struct.pack('<I', checksum) + payload
    return record + b'\xFF' * (256 - len(record))


def device_config_record(node_id, channel):
    if not 0 <= channel <= 32:
        raise ValueError('channel is outside 0..32')
    payload = struct.pack('<HBBHBBHBB160s', node_id, channel, 0, 0,
                          1, 0, 0, 0xFF, 0, bytes(160))
    prefix = struct.pack('<IHIH', CONFIG_MAGIC, CONFIG_SCHEMA, 1, len(payload))
    return prefix + struct.pack('<I', crc32(prefix + payload)) + payload


def provision(args):
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    requested_device_id = (getattr(args, 'device_id', None) or '').strip().upper()
    device_count = 0
    seen_uids = set()
    product_versions = None
    with Path(args.input).open(newline='', encoding='utf-8-sig') as source:
        reader = csv.DictReader(source)
        required = {
            'device_id', 'communication_mode', 'lora_channel',
            'firmware_version', 'hardware_version'
        }
        missing = required.difference(reader.fieldnames or ())
        if missing:
            raise SystemExit(f'input CSV missing columns: {", ".join(sorted(missing))}')
        for row in reader:
            device_id = ''.join(row.get('device_id', '').split()).upper()
            if not DEVICE_ID_PATTERN.fullmatch(device_id):
                raise SystemExit(f'invalid device_id: {row.get("device_id", "")}')
            if device_id in seen_uids:
                raise SystemExit(f'duplicate device_id: {device_id}')
            seen_uids.add(device_id)
            row['device_id'] = device_id
            communication_mode = row['communication_mode'].strip().lower()
            if communication_mode not in COMMUNICATION_MODES:
                raise SystemExit(f'{device_id}: communication_mode must be lora, 4g or both')
            try:
                channel = int(row['lora_channel'], 0)
            except ValueError as error:
                raise SystemExit(f'{device_id}: lora_channel must be an integer from 0 to 32') from error
            if not 0 <= channel <= 32:
                raise SystemExit(f'{device_id}: lora_channel must be from 0 to 32')
            firmware_version = row['firmware_version'].strip()
            hardware_version = row['hardware_version'].strip()
            try:
                parse_version(firmware_version)
            except argparse.ArgumentTypeError as error:
                raise SystemExit(f'{device_id}: invalid firmware_version: {error}') from error
            if not re.fullmatch(r'[A-Za-z0-9._-]{1,12}', hardware_version):
                raise SystemExit(f'{device_id}: hardware_version must be 1..12 ASCII letters, digits, dot, underscore or hyphen')
            if product_versions is None:
                product_versions = (firmware_version, hardware_version)
            elif product_versions != (firmware_version, hardware_version):
                raise SystemExit('all input CSV rows must use one firmware_version and hardware_version')
            if requested_device_id and device_id != requested_device_id:
                continue
            record = connectivity_record(row, args)
            filename = f'{device_id}-dataflash.bin'
            dataflash = bytearray(b'\xFF' * DATAFLASH_SIZE)
            config = device_config_record(int(device_id[-4:], 16), channel)
            dataflash[:len(config)] = config
            dataflash[0x2000:0x2000 + len(record)] = record
            (output_dir / filename).write_bytes(dataflash)
            device_count += 1
    if not device_count:
        if requested_device_id:
            raise SystemExit(f'device_id not found in input CSV: {requested_device_id}')
        raise SystemExit('input CSV contains no devices')
    print(f'{output_dir}: {device_count} DataFlash images')


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    ota_cmd = commands.add_parser('ota', help='export one raw .bin for BLE and HTTP OTA')
    ota_cmd.add_argument('--app', required=True)
    ota_cmd.add_argument('--version', required=False, type=parse_version,
                         help='optional build label retained for command compatibility')
    ota_cmd.add_argument('--output', required=True)
    ota_cmd.set_defaults(func=ota)

    full = commands.add_parser('factory', help='create one Intel HEX image for direct WCH-Link programming')
    full.add_argument('--jump', default='BLE/Peripheral/.pio/build/ch583_jump/firmware.bin')
    full.add_argument('--app', default='BLE/Peripheral/.pio/build/ch583/firmware.bin')
    full.add_argument('--updater', default='BLE/Peripheral/.pio/build/ch583_updater/firmware.bin')
    full.add_argument('--output', required=True)
    full.set_defaults(func=factory)

    package_cmd = commands.add_parser('package', help='export the production burn and OTA update firmware files')
    package_cmd.add_argument('--jump', default='BLE/Peripheral/.pio/build/ch583_jump/firmware.bin')
    package_cmd.add_argument('--app', default='BLE/Peripheral/.pio/build/ch583/firmware.bin')
    package_cmd.add_argument('--updater', default='BLE/Peripheral/.pio/build/ch583_updater/firmware.bin')
    package_cmd.add_argument('--output-dir', required=True)
    package_cmd.set_defaults(func=package)

    provision_cmd = commands.add_parser('provision', help='generate per-device DataFlash from the master CSV')
    provision_cmd.add_argument('--input', required=True, help='CSV with device_id,communication_mode,lora_channel,firmware_version,hardware_version')
    provision_cmd.add_argument('--output-dir', required=True)
    provision_cmd.add_argument('--device-id', help='generate only the selected device; used by the production flashing script')
    provision_cmd.add_argument('--broker', default='', help='optional initial Broker; may be configured later in the mini program')
    provision_cmd.add_argument('--port', type=int, default=1883)
    provision_cmd.add_argument('--keepalive', type=int, default=60)
    provision_cmd.add_argument('--qos', type=int, choices=(0, 1), default=0)
    provision_cmd.add_argument('--clean-session', action=argparse.BooleanOptionalAction, default=True)
    provision_cmd.add_argument('--pdp-type', type=int, choices=(0, 1), default=0)
    provision_cmd.add_argument('--network-timeout', type=int, default=120)
    provision_cmd.add_argument('--apn', default='')
    provision_cmd.add_argument('--report-interval', type=int, default=60)
    provision_cmd.add_argument('--register-sf', type=int, default=9)
    provision_cmd.add_argument('--register-bw', type=int, default=4)
    provision_cmd.add_argument('--listen-sf', type=int, default=10)
    provision_cmd.add_argument('--listen-bw', type=int, default=5)
    provision_cmd.set_defaults(func=provision)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
