# Multi-magnetic-switch adaptive core

This directory contains a hardware-independent calibration and travel-conversion core for keyboards that use analog magnetic sensors.

## What is implemented

- Per-key released and bottom endpoint calibration.
- Automatic detection of increasing or decreasing magnetic polarity.
- Per-key signal-span and noise evaluation.
- Capability tiers: 0.01 mm, 0.1 mm, digital-only, and incompatible.
- Median-of-three spike rejection.
- Optional monotonic 2-9 point raw-value lookup tables for measured switch curves.
- CRC and signature fields suitable for persistent per-key profiles.
- Fail-closed behavior for uncalibrated or incompatible switches.
- Optional integration with the existing Keychron HE action engine and raw-HID transport when `HE_SWITCH_ADAPTIVE_ENABLE=yes`.

All public travel values use 0.01 mm units. A value of 400 represents 4.00 mm.

## Required V6 Ultra HE board port

The public Keychron QMK tree does not currently contain an LPC5516 platform or a V6 Ultra HE target. Static analysis of the factory `2606021704` image has now recovered the LPC5516 raw-image format, the eight ADC GPIOs, the 6 x 21 scan geometry, the factory SRAM-code copy range, and the NXP ROM HID recovery path. The following information is still required before the adaptive core can be integrated as a production-quality V6 image:

1. The factory V6 Ultra HE source tree or its LPC5516 board-support package.
2. Authoritative mux-enable/control-net ownership and board-specific acquisition timing; the raw scan coordinate map is recovered, but manufacturing files contain no net names.
3. USB, RGB driver, EEPROM, wireless-module, mode-switch, battery, and wake pin assignments.
4. Linker script, clock tree, bootloader flash boundaries, and supported recovery procedure.
5. The complete factory raw-HID protocol expected by Keychron Launcher. The ROM bootloader VID/PID (`1fc9:0022`) is recovered, but it is not the normal application VID/PID.

Do not substitute the public STM32F401 K10 HE board files: the processor, ADC peripheral, GPIO assignments, bootloader, and binary format differ.

## Calibration sequence

For every changed key:

1. Reset `he_switch_calibration_t` with `he_adaptive_calibration_begin()`.
2. Select `HE_CALIBRATION_RELEASED`, leave the key untouched, and collect at least eight stable samples.
3. Select `HE_CALIBRATION_BOTTOMED_OUT`, hold the key at bottom, and collect at least eight stable samples.
4. Call `he_adaptive_calibration_finish()` and inspect the returned tier/status.
5. Optionally install a measured monotonic curve with `he_adaptive_profile_set_lut()`.
6. Persist the profile only after its CRC validates.
7. Feed runtime samples through `he_adaptive_update()`.

The optional raw-HID commands use the existing `0xA9` analog-matrix report family:

- `0x50`: begin per-key calibration (`row`, `col`).
- `0x51`: select released (`1`) or bottomed-out (`2`) sample phase.
- `0x52`: finish and classify the active key.
- `0x53`: read a key profile and its precision tier/status.
- `0x54`: clear a key profile and return to the factory conversion path.
- `0x55`: install a monotonic 2-9 point per-key raw-value LUT.
- `0x56`: force a calibrated key into mechanical-switch digital mode.

## Hybrid magnetic/mechanical sockets

The V6 Ultra HE V1.1 BOM contains both 108 TMR2611D sensors (`SW1-SW108`) and two mechanical socket contacts per key (`SWA_1-SWA_108` and `SWB_1-SWB_108`). Mechanical switches must not use magnetic travel precision. Calibrate their released/pressed endpoints, then call `he_adaptive_profile_force_digital()` (or raw-HID command `0x56`) so the key uses only the digital actuation/release hysteresis.

The board-level scanner must also isolate the nine CD4067B multiplexers during address changes, discard the first ADC conversion after switching, and confirm any multi-key transition before reporting it. The factory image proves that its high-frequency hybrid scanner runs from SRAM and combines analog and mechanical-contact bitmaps, but the remaining mux-control semantics and safe timing constants still require source, a netlist, or measurements on the real PCB.

Profiles are intentionally RAM-only in this hardware-independent layer. The V6 Ultra HE board port must persist them only after its real EEPROM/flash layout and wear-leveling boundaries are known. A power cycle therefore returns to the factory conversion path until the board-specific persistence layer is implemented.

The default quality thresholds are starting values, not production claims. They must be tuned using raw traces from the actual PCB and a calibrated displacement fixture.

## Host tests

```sh
cmake -S keyboards/keychron/common/analog_matrix/tests -B build/he-switch-tests
cmake --build build/he-switch-tests
ctest --test-dir build/he-switch-tests --output-on-failure
```
