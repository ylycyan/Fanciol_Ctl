# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Fanciol_Ctl is a BLE-connected HVAC fan coil controller firmware for the **WCH CH583M** (RISC-V RV32IMAC) microcontroller. The device — branded "ClimaSync" — controls split air conditioners via infrared, communicates with a cloud gateway over LoRa mesh, and is configured locally through a BLE-connected WeChat mini-program.

## Build Commands

The primary build system is **PlatformIO**, working directory is `BLE/Peripheral/`:

```bash
# Build firmware
cd BLE/Peripheral && pio run

# Upload via wch-link
cd BLE/Peripheral && pio run --target upload

# Clean build
cd BLE/Peripheral && pio run --target clean
```

The build produces an ELF and (via `post_extra_script.py`) an Intel HEX file. MounRiver Studio projects exist only for the OTA bootloader sub-projects (`BLE/BackupUpgrade_IAP/`, `BLE/BackupUpgrade_JumpIAP/`).

## Architecture

```
WeChat Mini Program (host)
       │
      BLE (service 0xFFE0: cmd/0xFFE1, passthrough/0xFFE2, status/0xFFE3)
       │
CH583M MCU ─── SX126x LoRa (421 MHz mesh → gateway)
  60 MHz         ├── HXD039B IR module (UART3 → AC unit)
  32 KB RAM      ├── NTC thermistor (ADC CH0, PA4)
  216 KB Flash   ├── DataFlash (persistent device config)
                 ├── Local rule engine (10 rules, time/temp/power triggers)
                 └── Energy metering (Wh, runtime, on/off cycles)
```

**Operating modes:** Local (mode=0, rules fire IR commands autonomously) and Remote (mode=1, LoRa gateway controls device).

**OTA scheme:** A/B firmware images at flash offsets 0x1000 and 0x37000, 12 KB bootloader at 0x6D000. Firmware is received over BLE OTA profile (service 0xFEE0).

## Key Directories

- `BLE/Peripheral/APP/` — Application source code (entry point: `peripheral_main.c`)
- `BLE/Peripheral/APP/include/board.h` — **Central header**: `t_dev` struct, all hardware pin definitions, BLE config, rule engine types, LoRa channel map
- `BLE/Peripheral/Profile/` — BLE GATT service implementations (custom profile, OTA, device info)
- `BLE/Peripheral/Ld/Link.ld` — Linker script (flash @ 0x1000, 216K; RAM 32K, stack 512B)
- `BLE/HAL/` — WCH HAL layer (MCU init, RTC, sleep)
- `BLE/LIB/` — Precompiled BLE stack (`libCH58xBLE.a`)
- `SRC/StdPeriphDriver/` — WCH CH58x peripheral drivers (ADC, Flash, GPIO, SPI, Timer, UART)
- `SRC/RVMSIS/` — RISC-V core support (PFIC interrupt controller, CSR access)

## BLE Protocol

Frame format: `[LEN][CMD][Payload][CRC]`, little-endian. CRC = (sum of all preceding bytes + 0xEC) & 0xFF.

Six command types: WRITE(0x01), READ(0x02), NOTIFY(0x03), ACK(0x04), ERROR(0x05), ACTION(0x06).

16 property IDs cover HVAC control (switch, mode, temp, fan, lock), IR config, LoRa config, system params, IR match/learn, and an aggregate ALL_STATE(0xF0).

Six action IDs: RESET, IR_MATCH, IR_LEARN, IR_CMD, SAVE_PARAMS, IR_LEARN_SEND.

Full protocol specification with byte-level examples: `BLE/Peripheral/下位机蓝牙通讯.md` (firmware side) and `BLE/Peripheral/上位机蓝牙协议实现文档.md` (host/mini-program side).

## Main Loop Timing

`Timer0` ISR fires every 10 ms. The main loop in `Main_Circulation()` (runs from RAM via `__HIGH_CODE`) polls:
- `Period_100ms()` — IR buffer check, LoRa state machine, LED blink, RTC update
- `Period_1s()` — Flash deferred write, ADC sampling, rule engine evaluation, energy metering, daily reset
- `TMOS_SystemProcess()` — WCH BLE stack event scheduler (must be called frequently)

## Device State Persistence

The `t_dev` struct (defined in `board.h`) holds all device state and is saved to CH583 DataFlash via `flash.c` with deferred-write debouncing. On first boot (magic code mismatch), defaults are initialized (nodeId=0xBB01, channel=5).

## Coding Conventions

- Bare-metal C (framework=`none`, no RTOS, no dynamic allocation)
- Interrupt-critical code marked `__INTERRUPT` or `__HIGH_CODE` (placed in RAM for speed)
- All peripheral access uses WCH StdPeriphDriver API (`PFIC_EnableIRQ`, `GPIOB_*`, `SPI0_*`, etc.)
- RISC-V architecture: `rv32imac`, ILP32 ABI, `-Os` optimization
- BLE stack is proprietary and precompiled — interact only through the HAL/LIB headers and TMOS API
