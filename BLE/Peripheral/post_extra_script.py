Import("env")

import csv
import re
from pathlib import Path


def parse_firmware_version(value):
    parts = value.split(".")
    if len(parts) not in (2, 3) or any(not part.isdigit() for part in parts):
        raise RuntimeError("firmware_version must be MAJOR.MINOR[.PATCH]")
    numbers = [int(part) for part in parts] + [0] * (3 - len(parts))
    if any(number > 255 for number in numbers):
        raise RuntimeError("firmware_version component exceeds 255")
    return (numbers[0] << 16) | (numbers[1] << 8) | numbers[2]


# production.csv is the single source for product software/hardware versions.
# All rows belong to the same firmware build and therefore must agree.
production_csv = Path(env.subst("$PROJECT_DIR")).parents[1] / "production.csv"
with production_csv.open(newline="", encoding="utf-8-sig") as source:
    rows = list(csv.DictReader(source))
if not rows:
    raise RuntimeError(f"{production_csv} contains no devices")
required = {"firmware_version", "hardware_version"}
missing = required.difference(rows[0])
if missing:
    raise RuntimeError(f"{production_csv} missing columns: {', '.join(sorted(missing))}")
firmware_versions = {row["firmware_version"].strip() for row in rows}
hardware_versions = {row["hardware_version"].strip() for row in rows}
if len(firmware_versions) != 1 or len(hardware_versions) != 1:
    raise RuntimeError("all production.csv rows must use one firmware_version and hardware_version")
firmware_text = firmware_versions.pop()
hardware_text = hardware_versions.pop()
if not re.fullmatch(r"[A-Za-z0-9._-]{1,12}", hardware_text):
    raise RuntimeError("hardware_version must be 1..12 ASCII letters, digits, '.', '_' or '-'")
firmware_number = parse_firmware_version(firmware_text)
env.Append(CPPDEFINES=[
    ("FIRMWARE_BUILD_VERSION", f"0x{firmware_number:08X}UL"),
    ("HARDWARE_BUILD_VERSION", f'\\"{hardware_text}\\"'),
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
