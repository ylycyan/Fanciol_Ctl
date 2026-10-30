"""Build version must come only from the two product header constants."""
from pathlib import Path
import contextlib
import io
import runpy
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'BLE/Peripheral/post_extra_script.py'


class BuildEnvironment:
    def __init__(self, project):
        self.project = project
        self.defines = []

    def subst(self, value):
        assert value == '$PROJECT_DIR'
        return str(self.project)

    def Append(self, CPPDEFINES):
        self.defines.extend(CPPDEFINES)

    def VerboseAction(self, *args):
        return args

    def AddPostAction(self, *args):
        pass


class ProductVersionTest(unittest.TestCase):
    def build(self, firmware='1.0.0', hardware='1.0.0', suffix=''):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            header = project / 'APP/include/product_version.h'
            header.parent.mkdir(parents=True)
            header.write_text(f'#define FIRMWARE_VERSION "{firmware}"\n'
                              f'#define HARDWARE_VERSION "{hardware}"\n{suffix}', encoding='utf-8')
            env = BuildEnvironment(project)
            with contextlib.redirect_stdout(io.StringIO()):
                runpy.run_path(str(SCRIPT), init_globals={'env': env, 'Import': lambda _: None})
            return dict(env.defines)

    def test_build_without_csv_and_one_numeric_define(self):
        self.assertEqual(self.build(), {'FIRMWARE_BUILD_VERSION': '0x00010000UL'})

    def test_multiple_digit_versions_and_maximum(self):
        self.assertEqual(self.build('10.20.30')['FIRMWARE_BUILD_VERSION'], '0x000A141EUL')
        self.assertEqual(self.build('255.255.255')['FIRMWARE_BUILD_VERSION'], '0x00FFFFFFUL')

    def test_invalid_versions_stop_build(self):
        for value in ('1.0', '1.0.0.0', '1.x.0', '-1.0.0', '256.0.0'):
            with self.subTest(value=value), self.assertRaisesRegex(RuntimeError, 'FIRMWARE_VERSION'):
                self.build(value)

    def test_missing_duplicate_or_invalid_hardware_stops_build(self):
        for value in ('', 'x' * 13, '1 0', '中文'):
            with self.subTest(value=value), self.assertRaisesRegex(RuntimeError, 'HARDWARE_VERSION'):
                self.build(hardware=value)
        with self.assertRaisesRegex(RuntimeError, 'exactly once'):
            self.build(suffix='#define FIRMWARE_VERSION "2.0.0"\n')

    def test_current_header_is_accepted(self):
        env = BuildEnvironment(ROOT / 'BLE/Peripheral')
        with contextlib.redirect_stdout(io.StringIO()):
            runpy.run_path(str(SCRIPT), init_globals={'env': env, 'Import': lambda _: None})
        self.assertEqual(set(dict(env.defines)), {'FIRMWARE_BUILD_VERSION'})


if __name__ == '__main__':
    unittest.main()
