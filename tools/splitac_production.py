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
CONNECTIVITY_MAGIC = 0x324D4F43
CONNECTIVITY_STATE_ACTIVE = 0xA1
CONNECTIVITY_SCHEMA = 3
CONFIG_MAGIC = 0x32434153
CONFIG_SCHEMA = 3
DATAFLASH_SIZE = 32 * 1024
DEVICE_UID_PATTERN = re.compile(r'^SAC\d{2}(?:0[1-9]|1[0-2])[A-Z]\d{7}$')


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
    version_text = f'{args.version >> 16}.{(args.version >> 8) & 0xFF}.{args.version & 0xFF}'
    if output.suffix.lower() != '.bin' or not re.search(rf'(?:^|[-_v]){re.escape(version_text)}$', output.stem, re.IGNORECASE):
        raise SystemExit(f'OTA output must be a versioned .bin file, for example firmware-{version_text}.bin')
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


def fixed_text(value, size, field):
    encoded = value.encode('ascii')
    if len(encoded) >= size or any(ch in value for ch in ('"', '\\')):
        raise ValueError(f'{field} must be ASCII and shorter than {size} bytes')
    return encoded + b'\0' * (size - len(encoded))


def connectivity_record(row, args):
    transport_value = row.get('transport_mask') or args.transport_mask
    transport = int(transport_value, 0) if isinstance(transport_value, str) else int(transport_value)
    node_id = int(row['node_id'], 0)
    if not 1 <= node_id <= 0xFFFF or not 1 <= transport <= 3:
        raise ValueError('node_id or transport_mask is invalid')
    payload = struct.pack(
        '<BBBBBBBBHHHH48s20s32s32s',
        transport, args.register_sf, args.register_bw,
        args.listen_sf, args.listen_bw, args.pdp_type, args.qos,
        1 if args.clean_session else 0, args.port, args.keepalive,
        args.report_interval, args.network_timeout,
        fixed_text(args.broker, 48, 'broker'),
        fixed_text(row.get('apn') or args.apn, 20, 'apn'),
        fixed_text(row['device_uid'], 32, 'device_uid'),
        fixed_text(args.topic_prefix, 32, 'topic_prefix')
    )
    prefix = struct.pack('<IBBIH', CONNECTIVITY_MAGIC, CONNECTIVITY_SCHEMA,
                         CONNECTIVITY_STATE_ACTIVE, 1, len(payload))
    checksum = crc32(prefix + payload)
    record = prefix + struct.pack('<I', checksum) + payload
    return node_id, record + b'\xFF' * (256 - len(record))


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
    rows_out = []
    seen_uids = set()
    with Path(args.input).open(newline='', encoding='utf-8-sig') as source:
        for row in csv.DictReader(source):
            device_uid = ''.join(row.get('device_uid', '').split()).upper()
            if not DEVICE_UID_PATTERN.fullmatch(device_uid):
                raise SystemExit(f'invalid device_uid: {row.get("device_uid", "")}')
            if device_uid in seen_uids:
                raise SystemExit(f'duplicate device_uid: {device_uid}')
            seen_uids.add(device_uid)
            row['device_uid'] = device_uid
            module_version = row.get('module_at_version', '').strip()
            if module_version != args.module_version:
                raise SystemExit(f'{device_uid}: ML307R firmware must be {args.module_version}, got {module_version or "empty"}')
            node_id, record = connectivity_record(row, args)
            channel_value = row.get('channel') or args.channel
            channel = int(channel_value, 0) if isinstance(channel_value, str) else int(channel_value)
            filename = f'{device_uid}-dataflash.bin'
            dataflash = bytearray(b'\xFF' * DATAFLASH_SIZE)
            config = device_config_record(node_id, channel)
            dataflash[:len(config)] = config
            dataflash[0x2000:0x2000 + len(record)] = record
            (output_dir / filename).write_bytes(dataflash)
            rows_out.append({
                'device_id': device_uid,
                'node_id': f'0x{node_id:04X}',
                'firmware_version': args.firmware,
                'production_batch': row.get('production_batch') or args.batch,
                'module_at_version': module_version,
                'mqtt_client_id': device_uid,
                'upload_topic': f'{args.topic_prefix}/{device_uid}/u',
                'control_topic': f'{args.topic_prefix}/{device_uid}/d',
                'dataflash_address': '0x70000',
                'dataflash_file': filename,
            })
    if not rows_out:
        raise SystemExit('input CSV contains no devices')
    csv_path = output_dir / 'platform-import.csv'
    with csv_path.open('w', newline='', encoding='utf-8-sig') as output:
        writer = csv.DictWriter(output, fieldnames=rows_out[0].keys())
        writer.writeheader()
        writer.writerows(rows_out)
    print(f'{csv_path}: {len(rows_out)} devices')


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    ota_cmd = commands.add_parser('ota', help='export one versioned raw .bin for BLE and HTTP OTA')
    ota_cmd.add_argument('--app', required=True)
    ota_cmd.add_argument('--version', required=True, type=parse_version)
    ota_cmd.add_argument('--output', required=True)
    ota_cmd.set_defaults(func=ota)

    full = commands.add_parser('factory', help='create one Intel HEX image for direct WCH-Link programming')
    full.add_argument('--jump', default='BLE/Peripheral/.pio/build/ch583_jump/firmware.bin')
    full.add_argument('--app', default='BLE/Peripheral/.pio/build/ch583/firmware.bin')
    full.add_argument('--updater', default='BLE/Peripheral/.pio/build/ch583_updater/firmware.bin')
    full.add_argument('--output', required=True)
    full.set_defaults(func=factory)

    provision_cmd = commands.add_parser('provision', help='generate per-device DataFlash and platform CSV')
    provision_cmd.add_argument('--input', required=True, help='CSV with device_uid,node_id,module_at_version and optional batch/APN columns')
    provision_cmd.add_argument('--output-dir', required=True)
    provision_cmd.add_argument('--broker', default='', help='optional initial Broker; may be configured later in the mini program')
    provision_cmd.add_argument('--port', type=int, default=1883)
    provision_cmd.add_argument('--keepalive', type=int, default=60)
    provision_cmd.add_argument('--qos', type=int, choices=(0, 1), default=0)
    provision_cmd.add_argument('--clean-session', action=argparse.BooleanOptionalAction, default=True)
    provision_cmd.add_argument('--pdp-type', type=int, choices=(0, 1), default=0)
    provision_cmd.add_argument('--network-timeout', type=int, default=120)
    provision_cmd.add_argument('--topic-prefix', default='sac/v1')
    provision_cmd.add_argument('--apn', default='')
    provision_cmd.add_argument('--report-interval', type=int, default=60)
    provision_cmd.add_argument('--transport-mask', type=int, default=3)
    provision_cmd.add_argument('--channel', type=int, default=5)
    provision_cmd.add_argument('--register-sf', type=int, default=9)
    provision_cmd.add_argument('--register-bw', type=int, default=4)
    provision_cmd.add_argument('--listen-sf', type=int, default=10)
    provision_cmd.add_argument('--listen-bw', type=int, default=5)
    provision_cmd.add_argument('--firmware', default='2.21.7')
    provision_cmd.add_argument('--module-version', default='MBRH0S01')
    provision_cmd.add_argument('--batch', default='')
    provision_cmd.set_defaults(func=provision)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
