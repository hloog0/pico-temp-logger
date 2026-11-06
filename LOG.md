# Log

One dated entry per session: what I did, time spent, what broke, the fix.

## 2026-10-31

- Arrival checks passed: chip marked RP2350, holding BOOTSEL mounts the RP2350 drive, headers inspected with no solder bridges.
- Toolchain check in a scratch `blink` project (SDK 2.3.1, board pico2, Arm): built `blink.uf2`, flashed it, changed the blink delay to 1000 ms, rebuilt and reflashed. LED blinked at 1 s as expected.
- Added USB serial output (`pico_enable_stdio_usb`, `stdio_init_all`, `printf`). `tick` lines appeared in the Serial Monitor.
- Broke: pressed the Code Runner ▶ button, which ran the Mac's host `gcc` and couldn't find `pico/stdlib.h`. Fix: build with the Pico extension's Compile/Run (cross-compiler `arm-none-eabi-gcc`).
- Broke: `printf` gave an implicit-declaration error (a hard error in GCC 14+). Fix: `#include <stdio.h>`.
- Created this repo's project (`pico-temp-logger`, USB stdio on, UART off).
- Time: ~20 min.

## 2026-11-05

- Read the built-in temperature sensor (ADC input 4, `hardware_adc`) and printed °C and volts over USB once per second.
- Reads ~28.5 °C against a room of ~22 °C (desk clock thermometer, unverified): about +6.5 °C, likely chip self-heating plus the datasheet's typical constants (0.706 V at 27 °C, −1.721 mV/°C).
- Pinching the chip raised it to ~32.8 °C, a change of only ~9 ADC counts. Resolution ≈ 3.3 V / 4096 ≈ 0.81 mV ≈ 0.47 °C per count.
- Broke: pressed Code Runner's ▶ again (host `gcc`). Fix: hid its button in `.vscode/settings.json`.
- Broke: `%d` used with a `float` printed garbage. Fix: `%.2f` for °C, `%.4f` for volts (2 decimals hid the change).
- Time: ~50 min.
