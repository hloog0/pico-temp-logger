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

## 2026-11-09

- Prediction before measuring timestamp gaps (written before running `tools/jitter.py`): mean 1,000,000 µs, min/max within ±3 µs, std dev ~0.1 µs.
- First run (11 gaps): 10 gaps exactly 1,000,000 µs, 1 gap 999,997 µs (one sample noticed ~3 µs late). Mean 999,999.73 µs, std dev 0.90 µs.
- Prediction before 300-gap run: ~270/300 gaps exactly 1,000,000 µs; late samples typically ~3 µs, worst ~10 µs.
- Switched sampling from `sleep_ms` to a hardware repeating timer (`add_repeating_timer_ms(-1000, ...)`): callback only sets a `volatile` flag; main loop clears it first, then timestamps with `time_us_64()`, reads, prints CSV `timestamp_us,temp_c,volts`.
- Wrote `tools/jitter.py` (pyserial): reads N lines, skips malformed lines, reports gap min/max/mean/std dev, a count of each gap value, and positions of gaps ≠ 1,000,000 µs.
- 300-gap run: 299 gaps exactly 1,000,000 µs, 1 gap 999,995 µs. Two 100-gap runs: 99/100 exact each, the odd gap 999,996 and 999,997 µs.
- Finding: in all 4 runs the only off gap was the first one, short by 3–5 µs. Hypothesis: the first sample after the laptop opens the port is noticed late while the Pico handles USB setup traffic. In steady state, every tick was noticed within the 1 µs clock resolution. Caveat: timer and timestamps share one clock, so this measures loop latency, not timer accuracy; oscilloscope check still to do.
- Prediction vs result: predicted ~270/300 exact and 3–10 µs late samples; got 299/300 and a single 5 µs startup offset.
- Broke: `while len(timestamps) <= 11` collected 12 timestamps (11 gaps, not 10). Fix: name the count `n_gaps` and print `len(gaps)`.
- Time: ~2 h 15 min.
