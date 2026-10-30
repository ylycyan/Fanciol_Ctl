import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import splitac_production as production


class FirmwarePackagingTest(unittest.TestCase):
    def test_packaging_has_no_device_configuration_entry(self):
        result = subprocess.run([sys.executable, str(ROOT / 'tools/splitac_production.py'), '--help'],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('provision', result.stdout)
        self.assertNotIn('factory', result.stdout)
        self.assertNotIn('ota', result.stdout)
        self.assertFalse(hasattr(production, 'connectivity_record'))

    def read_hex(self, path):
        memory = {}
        upper = 0
        lines = path.read_text(encoding='ascii').splitlines()
        self.assertEqual(lines[-1], ':00000001FF')
        for line in lines:
            record = bytes.fromhex(line[1:])
            self.assertEqual(sum(record) & 255, 0)
            count, high, low, kind = record[:4]
            self.assertEqual(len(record), count + 5)
            payload = record[4:-1]
            if kind == 4:
                upper = int.from_bytes(payload, 'big') << 16
            elif kind == 0:
                address = upper + (high << 8) + low
                for index, value in enumerate(payload):
                    self.assertNotIn(address + index, memory)
                    memory[address + index] = value
        return memory

    def test_package_exports_complete_and_addressed_components(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            jump = root / 'jump.bin'
            app = root / 'app.bin'
            updater = root / 'updater.bin'
            output = root / 'release'
            jump.write_bytes(b'\x01\x10\x00\x00')
            app.write_bytes(bytes(range(256)) * 257)  # Cross a 64 KiB HEX boundary.
            updater.write_bytes(bytes(range(64)))

            production.package(SimpleNamespace(
                jump=str(jump), app=str(app), updater=str(updater),
                output_dir=str(output)
            ))

            self.assertEqual(
                sorted(path.name for path in output.iterdir()),
                ['app.bin', 'app.hex', 'bootload.hex', 'sacc26.hex']
            )
            self.assertEqual((output / 'app.bin').read_bytes(), app.read_bytes())
            expected_app = dict(enumerate(app.read_bytes(), 0x1000))
            expected_boot = dict(enumerate(jump.read_bytes()))
            expected_boot.update(enumerate(updater.read_bytes(), 0x6D000))
            self.assertEqual(self.read_hex(output / 'app.hex'), expected_app)
            self.assertEqual(self.read_hex(output / 'bootload.hex'), expected_boot)
            self.assertEqual(self.read_hex(output / 'sacc26.hex'), {**expected_boot, **expected_app})

    def test_invalid_component_does_not_export_partial_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            jump, app, updater = [root / name for name in ('jump.bin', 'app.bin', 'updater.bin')]
            jump.write_bytes(bytes(4))
            app.write_bytes(bytes(96))
            updater.write_bytes(bytes(12 * 1024 + 1))
            output = root / 'release'
            args = SimpleNamespace(jump=jump, app=app, updater=updater, output_dir=output)
            with self.assertRaisesRegex(SystemExit, 'Updater size'):
                production.package(args)
            self.assertFalse(output.exists())
            updater.write_bytes(bytes(64))
            app.write_bytes(bytes(63))
            with self.assertRaisesRegex(SystemExit, 'application size'):
                production.package(args)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
