import csv
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import splitac_production as production


def provision_args(source, output):
    return SimpleNamespace(
        input=str(source), output_dir=str(output), broker='mqtt.internal', port=1883,
        keepalive=60, qos=0, clean_session=True, pdp_type=0, network_timeout=120,
        topic_prefix='sac/v1', apn='', report_interval=60, transport_mask=3,
        channel=5, register_sf=9, register_bw=4, listen_sf=10, listen_bw=5,
        firmware='2.21.6', module_version='MBRH0S01', batch='202608-A'
    )


class ProductionProvisioningTest(unittest.TestCase):
    def write_csv(self, path, rows):
        with path.open('w', newline='', encoding='utf-8') as output:
            writer = csv.DictWriter(output, fieldnames=('device_uid', 'node_id', 'module_at_version'))
            writer.writeheader()
            writer.writerows(rows)

    def test_factory_uid_drives_image_name_contents_and_platform_topics(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'production.csv'
            output = root / 'out'
            self.write_csv(source, [{
                'device_uid': 'SAC2608A0000001', 'node_id': '0x1001',
                'module_at_version': 'MBRH0S01'
            }])

            production.provision(provision_args(source, output))

            image = output / 'SAC2608A0000001-dataflash.bin'
            self.assertTrue(image.exists())
            self.assertIn(b'SAC2608A0000001', image.read_bytes())
            with (output / 'platform-import.csv').open(newline='', encoding='utf-8-sig') as source_file:
                row = next(csv.DictReader(source_file))
            self.assertEqual(row['device_id'], 'SAC2608A0000001')
            self.assertEqual(row['mqtt_client_id'], 'SAC2608A0000001')
            self.assertEqual(row['control_topic'], 'sac/v1/SAC2608A0000001/d')

    def test_invalid_or_duplicate_factory_uid_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'production.csv'
            rows = [
                {'device_uid': 'SAC2608A0000001', 'node_id': '0x1001', 'module_at_version': 'MBRH0S01'},
                {'device_uid': 'SAC2608A0000001', 'node_id': '0x1002', 'module_at_version': 'MBRH0S01'}
            ]
            self.write_csv(source, rows)
            with self.assertRaisesRegex(SystemExit, 'duplicate device_uid'):
                production.provision(provision_args(source, root / 'duplicate'))

            rows[0]['device_uid'] = 'SAC2613A0000001'
            self.write_csv(source, rows[:1])
            with self.assertRaisesRegex(SystemExit, 'invalid device_uid'):
                production.provision(provision_args(source, root / 'invalid'))

    def test_ota_exports_the_application_as_one_versioned_raw_bin(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'application.bin'
            output = root / 'firmware-2.22.0.bin'
            image = bytes(range(96))
            source.write_bytes(image)
            production.ota(SimpleNamespace(app=str(source), version=0x00021600, output=str(output)))
            self.assertEqual(output.read_bytes(), image)
            with self.assertRaisesRegex(SystemExit, 'versioned .bin'):
                production.ota(SimpleNamespace(app=str(source), version=0x00021600,
                                                output=str(root / 'firmware.bin')))


if __name__ == '__main__':
    unittest.main()
