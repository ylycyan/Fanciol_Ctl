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
        device_id=None,
        username='ecac', password='ecac2026',
        publish_topic='pub/ac/{uid}', subscribe_topic='sub/ac/{uid}',
        keepalive=60, qos=0, clean_session=True, pdp_type=0, network_timeout=120,
        apn='', report_interval=60,
        register_sf=9, register_bw=4, listen_sf=10, listen_bw=5
    )


class ProductionProvisioningTest(unittest.TestCase):
    def write_csv(self, path, rows):
        with path.open('w', newline='', encoding='utf-8') as output:
            writer = csv.DictWriter(output, fieldnames=(
                'device_id', 'communication_mode', 'lora_channel',
                'firmware_version', 'hardware_version', 'mqtt_host'))
            writer.writeheader()
            writer.writerows(rows)

    def test_device_id_and_link_settings_drive_dataflash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'production.csv'
            output = root / 'out'
            self.write_csv(source, [{
                'device_id': 'A26091001',
                'communication_mode': 'lora', 'lora_channel': '9',
                'firmware_version': '2.22.19', 'hardware_version': 'HW1.0',
                'mqtt_host': '106.15.11.119'
            }])

            production.provision(provision_args(source, output))

            image = output / 'A26091001-dataflash.bin'
            self.assertTrue(image.exists())
            dataflash = image.read_bytes()
            self.assertIn(b'A26091001', dataflash)
            self.assertEqual(int.from_bytes(dataflash[16:18], 'little'), 0x1001)
            self.assertEqual(dataflash[18], 9)
            self.assertEqual(dataflash[0x2010], 1)
            self.assertEqual(dataflash[0x2020:0x202D], b'106.15.11.119')
            self.assertFalse((output / 'platform-import.csv').exists())

    def test_invalid_or_duplicate_factory_uid_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'production.csv'
            rows = [
                {'device_id': 'A26091001',
                 'communication_mode': 'lora', 'lora_channel': '9',
                 'firmware_version': '2.22.19', 'hardware_version': 'HW1.0'},
                {'device_id': 'A26091001',
                 'communication_mode': '4g', 'lora_channel': '0',
                 'firmware_version': '2.22.19', 'hardware_version': 'HW1.0'}
            ]
            self.write_csv(source, rows)
            with self.assertRaisesRegex(SystemExit, 'duplicate device_id'):
                production.provision(provision_args(source, root / 'duplicate'))

            rows[0]['device_id'] = 'A26131001'
            self.write_csv(source, rows[:1])
            with self.assertRaisesRegex(SystemExit, 'invalid device_id'):
                production.provision(provision_args(source, root / 'invalid'))

    def test_production_script_can_generate_only_scanned_device(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'production.csv'
            output = root / 'out'
            self.write_csv(source, [
                {'device_id': 'A26091001',
                 'communication_mode': 'lora', 'lora_channel': '9',
                 'firmware_version': '2.22.19', 'hardware_version': 'HW1.0'},
                {'device_id': 'A26091002',
                 'communication_mode': '4g', 'lora_channel': '0',
                 'firmware_version': '2.22.19', 'hardware_version': 'HW1.0'}
            ])
            args = provision_args(source, output)
            args.device_id = 'a26091002'

            production.provision(args)

            self.assertFalse((output / 'A26091001-dataflash.bin').exists())
            self.assertTrue((output / 'A26091002-dataflash.bin').exists())

            args.device_id = 'A2609FFFF'
            with self.assertRaisesRegex(SystemExit, 'device_id not found'):
                production.provision(args)

    def test_invalid_communication_mode_or_lora_channel_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'production.csv'
            row = {
                'device_id': 'A26091001',
                'communication_mode': 'wifi', 'lora_channel': '9',
                'firmware_version': '2.22.19', 'hardware_version': 'HW1.0'
            }
            self.write_csv(source, [row])
            with self.assertRaisesRegex(SystemExit, 'communication_mode'):
                production.provision(provision_args(source, root / 'mode'))
            row['communication_mode'] = '4g'
            row['lora_channel'] = '33'
            self.write_csv(source, [row])
            with self.assertRaisesRegex(SystemExit, 'lora_channel'):
                production.provision(provision_args(source, root / 'channel'))

    def test_mixed_product_versions_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'production.csv'
            rows = [
                {'device_id': 'A26091001',
                 'communication_mode': 'lora', 'lora_channel': '9',
                 'firmware_version': '2.22.19', 'hardware_version': 'HW1.0'},
                {'device_id': 'A26091002',
                 'communication_mode': '4g', 'lora_channel': '0',
                 'firmware_version': '2.22.20', 'hardware_version': 'HW1.0'}
            ]
            self.write_csv(source, rows)
            with self.assertRaisesRegex(SystemExit, 'one firmware_version and hardware_version'):
                production.provision(provision_args(source, root / 'mixed'))

    def test_ota_exports_the_application_with_any_bin_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'application.bin'
            output = root / 'firmware.bin'
            image = bytes(range(96))
            source.write_bytes(image)
            production.ota(SimpleNamespace(app=str(source), version=0x00021600, output=str(output)))
            self.assertEqual(output.read_bytes(), image)
            with self.assertRaisesRegex(SystemExit, '.bin extension'):
                production.ota(SimpleNamespace(app=str(source), version=0x00021600,
                                                output=str(root / 'firmware.hex')))

    def test_package_exposes_only_burn_and_update_firmware(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            jump = root / 'jump.bin'
            app = root / 'app.bin'
            updater = root / 'updater.bin'
            output = root / 'release'
            jump.write_bytes(b'\x01\x10\x00\x00')
            app.write_bytes(bytes(range(96)))
            updater.write_bytes(bytes(range(64)))

            production.package(SimpleNamespace(
                jump=str(jump), app=str(app), updater=str(updater),
                output_dir=str(output)
            ))

            self.assertEqual(
                sorted(path.name for path in output.iterdir()),
                ['splitac-burn.hex', 'splitac-update.bin']
            )
            self.assertEqual((output / 'splitac-update.bin').read_bytes(), app.read_bytes())
            self.assertTrue((output / 'splitac-burn.hex').read_text(encoding='ascii').endswith(':00000001FF\n'))


if __name__ == '__main__':
    unittest.main()
