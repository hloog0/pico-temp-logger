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

## 2026-11-15

- Read DS18B20 datasheet pp. 10–16 (1-Wire signaling, transaction sequence, ROM and function commands).
- Prediction before writing the driver: bit timing will be the hardest and most finicky part; the function commands will be the most straightforward.
- Wired probe 1: probe → DROK adapter screw terminal (yellow DAT, red VCC, black GND; adapter has 4.7 kΩ pull-up) → jumpers to Pico: orange VCC → 3V3 (pin 36), green GND → GND (pin 38), yellow DAT → GP2 (pin 4). Caught GND jumper on VSYS/VBUS in photo check before power-up; continuity 3V3–GND checked.
- Wrote `onewire_reset()`: drive low 480 µs (output, value 0), release (input), sample at 70 µs (inside the guaranteed-low 60–75 µs presence window), wait 410 µs. Returns `!line_high`.
- Tests passed: probe connected → presence detected; jumper off GP2 → no device; probe data wire unscrewed → no device.
- Broke: used `gpio_pull_down/up` (weak ~50 kΩ internal pulls, lose to the 4.7 kΩ) instead of switching direction; used physical pin 4 instead of GPIO 2; return value inverted (low = present). Floating pin read low via default pull-down → added `gpio_pull_up` as backup.
- Prediction before first Read ROM [33h] run: rom[0] = 0x33 (echo of the command); line identical every second; bit timing errors would show as nonsense bytes.
- Read ROM [33h] result, probe 1: `28 12 93 D5 00 00 00 52`. rom[0] = 0x28 = DS18B20 family code, not an echo of 0x33 (prediction wrong: writing sends a command; the probe answers with its own ID). CRC byte (0x52) not yet verified.
- Time, first block: ~3 h 40 min (1:39–5:19 pm). Break for dinner; next: CRC-8 check (step 6) and probe 2 Read ROM.
- Second block started 6:46 pm.
- Prediction before CRC check: probe 1 ROM passes; forcing a changed ROM byte should make it fail.
- Wrote `onewire_crc8()` (Dallas/Maxim CRC-8, reflected polynomial 0x8C, LSB first). Probe 1 ROM: CRC OK every read. Negative test: flipped one bit (`rom[3] ^= 0x01`) → CRC FAIL; flip removed. Prediction confirmed.
- Prediction before measuring conversion time: default 12-bit, datasheet max 750 ms; expect 500–700 ms.
- Conversion-time poll returned 0.0 ms every time (first read slot already 1) vs predicted 500–700 ms. Debugging: hypotheses = parasite power (VCC not reaching probe), clone chip without busy-polling, or conversion not starting. Adapter VCC–GND measured 3.284 V while powered.
- Prediction before Read Power Supply [B4h]: externally powered.
- Read Power Supply [B4h]: externally powered (prediction confirmed). Rules out parasite power as the cause of the 0.0 ms poll. Remaining: clone without busy-polling, or conversion not starting.
- Prediction before fixed-wait scratchpad read: bytes 0–1 = 50 05 (85.0 °C power-on default), i.e. conversion never happened.
- Fixed 750 ms wait then Read Scratchpad [BEh], probe 1: `75 01 7F 80 7F FF 0B 10 F7`, CRC OK. Bytes 0–1 ≠ 50 05, so the conversion DID happen (prediction wrong). Config byte = 0x7F (12-bit). Conclusion: this probe does not signal "busy" with 0 read slots during conversion, so polling can't measure conversion time. Genuine vs clone not determined from these bytes alone. Decision: fixed 750 ms (datasheet max) wait.
- Broke along the way: compared `scratch[0] == 50` (decimal) instead of `0x50`; called `ds18b20_is_ext_powered` without `()` (always true).
- Decoded by hand: bytes 0–1 `75 01` → 0x0175 = 373 counts; 12-bit resolution = 1/16 °C per count (fixed-point, 4 fractional bits) → 373 / 16 = 23.3125 °C. Prediction ("between the desk clock ~22 °C and the built-in sensor ~28.5 °C") correct; much closer to the desk clock.
- Wrote `ds18b20_raw_to_celsius()` (int16_t from bytes 1:0, / 16.0f). Live readings: 22.1875 °C (0x0163), 22.2500 °C (0x0164) — 0.0625 °C steps, scratchpad CRC OK, ROM CRC OK each tick. Probe 1 end-to-end on own driver: done.
- Time, second block: ~2 h (6:46–8:40 pm). Day total ~5 h 40 min.
