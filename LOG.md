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

## 2026-11-22

- Started ~8:27 pm. Week plan: tonight two probes on one pin (Match ROM); Mon–Wed flash logging; 48 h test starts by Fri morning (fallback: USB serial logging if flash dump after power cycle isn't working by Wed night).
- Prediction before probe 2 Read ROM: rom[0] = 0x28 same as probe 1; the rest different, except `00 00 00` may stay (thought they were reserved bytes). Temperature colder than probe 1's Nov 15 reading (later in the year, colder dorm).
- Probe 2 alone (swapped into probe 1's adapter, same firmware): ROM `28 72 C0 D3 00 00 00 43`, CRC OK, externally powered. Scratchpad e.g. `5A 01 7F 80 7F FF 06 10 2C`, CRC OK, config 0x7F (12-bit). Readings 21.6250 → 21.5625 → 21.5000 °C over ~5 s.
- Family code prediction correct. `00 00 00` did stay, but they're not reserved: they're the high bytes of the 48-bit serial number (bytes 1–6), which happen to be zero on both probes. CRC byte changed (0x52 → 0x43), as it must when the serial changes.
- Temperature prediction: colder (~21.5 vs ~22.2 °C), but this comparison can't separate probe offset from room temperature on different days. Probe-to-probe difference needs both probes read in the same tick. Readings falling right after wiring suggest the probe was still warm from handling.
- Wired both probes into ONE adapter (option A: two wires twisted under each screw terminal) → one 4.7 kΩ pull-up on the bus. Considered option B (breadboard, both adapters) = two pull-ups in parallel ≈ 2.35 kΩ, ~1.4 mA sink, also within the probe's 4 mA rating; chose A as simpler and matching the datasheet's single pull-up.
- Prediction before running the unchanged single-probe firmware with both probes: the bus is a wired-AND (any device pulling low wins), so Read ROM returns the bitwise AND of the two IDs: `28&28 12&72 93&C0 D5&D3 00 00 00 52&43` = `28 12 80 D1 00 00 00 42`; CRC FAIL.
- Result: `28 12 80 D1 00 00 00 42` every tick, CRC FAIL; Read Scratchpad with Skip ROM → CRC FAIL; Read Power Supply with Skip ROM → externally powered. Prediction exactly correct. Measured evidence that 1-Wire is a wired-AND: Skip ROM is fine for commands nobody answers (Convert T) and for 1-bit "anyone?" questions (B4h: 1 means ALL probes are externally powered), but anything multi-byte that a probe sends back (Read ROM, Read Scratchpad) collides → needs Match ROM [55h] + 8-byte ID.
- Code: stored both IDs in `const uint8_t probe_rom[2][8]`; wrote `onewire_match_rom(pin, rom)` (sends 55h + 8 ID bytes, no reset — caller owns it); `ds18b20_read_scratchpad(pin, rom, scratch)` now uses Match ROM instead of Skip ROM; deleted Read ROM from main; loop = one Skip ROM Convert T (both convert at once) → 750 ms → Match ROM read per probe → difference only if both `ok[p]` (CRC OK AND not the 50 05 power-on value).
- Broke/caught in review: first match_rom draft sent CC then 55 and read the ID instead of writing it (const caught the write-to-rom); passed `probe_rom` instead of `probe_rom[p]` (pointer-to-row vs pointer-to-byte — would have worked for probe 1 by accident); difference gated on `temp_c[0] && temp_c[1]` (stale/uninitialized data, and a real 0.0 °C reading would count as false) → fixed to `ok[0] && ok[1]`; 50 05 branch left ok[p] true → set false; function defined after its caller → moved up; unsaved files 3×.
- Prediction before two-probe Match ROM run: CRC OK on both (each probe now answers alone). Accuracy ±0.5 °C each (datasheet p. 1) → worst-case in-spec difference 1.0 °C; expected ~0.4 °C with tips taped together after ~5 min.
- Result, two probes on GP2 via Match ROM (tips taped together, untouched 10:06–10:11 pm, 16 consecutive ticks captured at the end): CRC OK on both probes every tick (16/16 each). Temperatures 18.5000–18.6250 °C. Difference (probe 1 − probe 2): 0.0000 °C in 11/16 ticks, ±0.0625 °C (one count) in 5/16; never more than one count.
- CRC prediction correct. Difference prediction (~0.4 °C) wrong: measured difference is at the resolution limit (±0.0625 °C), well inside the 1.0 °C worst case for two ±0.5 °C probes. The ±1-count flicker is quantization: near a count boundary each probe rounds independently.
- Caveat: probes AGREEING doesn't show either is ACCURATE — both could share the same offset (likely same batch). Checking accuracy needs a reference thermometer. Also: room read ~21.5 °C at 8:36 pm and ~18.6 °C at 10:11 pm (dorm cooling at night, not investigated).
- Both scratchpads again show byte 6 = 16 − (byte 0 & 0x0F) (29→07, 2A→06, 28→08). Noted for the later genuine-vs-clone question.
- Decision point (two-probe CRC-checked reads, planned Nov 21, met Nov 22): keep own driver, no library.
- Not done tonight: built-in sensor in the same tick; interrupt protection for reset's release→sample (70 µs sample vs 75 µs window edge).
- Time: ~1 h 45 min (8:27–10:13 pm).
- Open question (10:17 pm): food probe read 21.6 °C, desk clock 69.6 °F ≈ 20.9 °C (both fluctuating), vs DS18B20s 18.6 °C at 10:11 pm — a 2–3 °C gap, beyond ±0.5 °C. Not co-located, so not yet a fair comparison. Hypotheses: (1) probe location (desk surface/window/vent), (2) references inaccurate (±1–2 °C typical, single readings), (3) shared DS18B20 offset. Self-heating ruled out by direction (would read high, not low). Next: co-locate food probe with taped DS18B20 tips in open air, 5 min hands-off, average 5 readings.
