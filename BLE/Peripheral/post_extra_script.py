Import("env")

import re
from pathlib import Path


def parse_firmware_version(value):
    parts = value.split(".")
    if len(parts) != 3 or any(not part.isdigit() for part in parts):
        raise RuntimeError("FIRMWARE_VERSION must be MAJOR.MINOR.PATCH")
    numbers = [int(part) for part in parts]
    if any(number > 255 for number in numbers):
        raise RuntimeError("FIRMWARE_VERSION component exceeds 255")
    return (numbers[0] << 16) | (numbers[1] << 8) | numbers[2]


# Keep the wire/OTA integer derived from the same source as the C strings.
version_header = Path(env.subst("$PROJECT_DIR")) / "APP/include/product_version.h"
version_source = version_header.read_text(encoding="utf-8-sig")
versions = {}
for name in ("FIRMWARE_VERSION", "HARDWARE_VERSION"):
    values = re.findall(r'^\s*#define\s+' + name + r'\s+"([^"\r\n]+)"\s*$',
                        version_source, re.MULTILINE)
    if len(values) != 1:
        raise RuntimeError(f"{version_header}: define {name} exactly once as a string")
    versions[name] = values[0]
firmware_text = versions["FIRMWARE_VERSION"]
hardware_text = versions["HARDWARE_VERSION"]
if not re.fullmatch(r"[A-Za-z0-9._-]{1,12}", hardware_text):
    raise RuntimeError("HARDWARE_VERSION must be 1..12 ASCII letters, digits, '.', '_' or '-'")
firmware_number = parse_firmware_version(firmware_text)
env.Append(CPPDEFINES=[
    ("FIRMWARE_BUILD_VERSION", f"0x{firmware_number:08X}UL"),
])
print(f"Product versions: firmware={firmware_text} hardware={hardware_text}")

# Generate .hex file from .elf
env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(
        "$OBJCOPY -O ihex $BUILD_DIR/${PROGNAME}.elf $BUILD_DIR/${PROGNAME}.hex",
        "Building $BUILD_DIR/${PROGNAME}.hex"
    )
)


