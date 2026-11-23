# pico-temp-logger

A temperature logger on a Raspberry Pi Pico 2 (RP2350) using DS18B20 digital temperature probes. I wrote the 1-Wire driver myself in C from the DS18B20 datasheet instead of using a library.

**Status: in progress.** Two probes share one data wire and are read one at a time by their 64-bit IDs, with CRC-checked temperatures through my own driver. Next: logging to onboard flash, a 48-hour indoor test, and a week-long outdoor run. Dated progress is in [LOG.md](LOG.md).

## What works now

- **1-Wire driver** (bit-banged on one GPIO): reset/presence detection, read and write time slots with interrupts disabled only during the timing-critical part of each slot, byte read/write (LSB first), and Dallas/Maxim CRC-8.
- **DS18B20 commands:** Read ROM (`33h`), Skip ROM (`CCh`), Match ROM (`55h`), Convert T (`44h`), Read Scratchpad (`BEh`), Read Power Supply (`B4h`).
- **Two probes on one wire:** one broadcast Convert T starts both probes; each is then read separately by its 64-bit ID.
- **Temperature readout:** 12-bit resolution (0.0625 °C steps), every reading checked with CRC-8.
- **Timer-driven sampling:** a hardware repeating timer sets a flag once per second; the main loop does the work.
- **Built-in sensor:** the RP2350's internal temperature sensor read through the ADC, for comparison.
- **`tools/jitter.py`:** a Python script that reads timestamps over USB serial and reports sample-interval statistics.

## Hardware

- Raspberry Pi Pico 2 (non-W), pre-soldered headers
- 2× DROK waterproof DS18B20 probes, each with a DROK adapter board (screw terminal + 4.7 kΩ pull-up between DAT and VCC)
- Female–female jumper wires, USB-A to micro-USB cable

## Wiring

Both probes run in external power mode (3.3 V) and share **one** adapter: each screw terminal holds the matching wire from both probes, twisted together. That keeps a single 4.7 kΩ pull-up on the bus, as in the datasheet. (Two adapters on a breadboard would also work: two 4.7 kΩ resistors in parallel make about 2.35 kΩ, so a probe sinks about 1.4 mA to pull the line low, within its 4 mA rating.)

| Probe wires | Adapter terminal | Jumper color | Pico 2 pin |
|---|---|---|---|
| Red (both probes) | VCC | Orange | 3V3(OUT), physical pin 36 |
| Black (both probes) | GND | Green | GND, physical pin 38 |
| Yellow (both probes) | DAT | Yellow | GP2 (GPIO 2), physical pin 4 |

Probe IDs, read with Read ROM while each probe was the only one connected:

| Probe | 64-bit ROM ID (family code first, CRC last) |
|---|---|
| 1 | `28 12 93 D5 00 00 00 52` |
| 2 | `28 72 C0 D3 00 00 00 43` |

The 4.7 kΩ resistor pulls the data line up to 3.3 V. The firmware also enables the RP2350's weak internal pull-up as a backup, so a disconnected data wire reads as "no device" instead of floating.

## How it works

The code is built in layers; each layer only uses the one below it.

```
DS18B20 commands   ds18b20_start_conversion, ds18b20_read_scratchpad, ...
1-Wire bytes       onewire_write_byte / onewire_read_byte (LSB first), onewire_crc8,
                   onewire_match_rom (55h + 8-byte ID)
1-Wire bit slots   onewire_reset, onewire_write_bit, onewire_read_bit
Wire               one data line; the resistor holds it high, devices only pull it low
```

### 1-Wire bit timing

The Pico never drives the line high. To pull the line low it switches the pin to output (output value fixed at 0); to release it, it switches the pin to input and lets the pull-up resistor raise the line.

| Slot | Pico holds low | Then | Why |
|---|---|---|---|
| Reset | 480 µs | release, sample at 70 µs, wait 410 µs | The presence pulse is guaranteed low only from 60 µs (latest start) to 75 µs (earliest end) after release |
| Write 1 | 5 µs | release, wait 60 µs | Line must be high again before the probe samples (15–60 µs) |
| Write 0 | 80 µs | release, wait 5 µs | Line must still be low at the probe's latest sample (60 µs) |
| Read | 3 µs | release, sample at 10 µs, wait 50 µs | Data is valid only within 15 µs of the slot start |

Interrupts are turned off from the start of each slot until its release (write) or sample (read), so a USB or timer interrupt can't push a sample past its window. They stay on during the long waits, so they're held off for at most ~85 µs. **Not yet protected:** the reset's release-to-sample stretch. It has the tightest margin in the driver (sample at 70 µs, window closes at 75 µs), so it's the next fix.

### Reading a temperature

1. Reset → Skip ROM → Convert T (both probes start converting at once)
2. Wait 750 ms (the datasheet maximum at 12-bit; see "What broke" for why it's a fixed wait)
3. For each probe: Reset → Match ROM + its 8-byte ID → Read Scratchpad → 9 bytes → check CRC-8
4. Bytes 0–1 form a signed 16-bit value (low byte first); divide by 16 for °C. Signed matters below 0 °C, where the probe sends two's-complement negatives.
5. A reading counts only if its CRC passes **and** it isn't `50 05` (the 85.0 °C power-on value, which means no conversion happened). The probe-to-probe difference is printed only when both readings count, so a failed read never shows up as last second's value.

### Two probes on one wire

The data line is open-drain: devices can only pull it low, and the resistor pulls it back high. So when two probes answer at the same time, the line reads low if **either** one pulls it low. Each bit the Pico sees is the AND of both probes' bits.

I tested this before writing any two-probe code. With both probes connected and the single-probe firmware unchanged, I predicted that Read ROM would return the bitwise AND of the two IDs:

| | Byte 0 | 1 | 2 | 3 | 4–6 | 7 (CRC) |
|---|---|---|---|---|---|---|
| Probe 1 | 28 | 12 | 93 | D5 | 00 00 00 | 52 |
| Probe 2 | 28 | 72 | C0 | D3 | 00 00 00 | 43 |
| Predicted AND | 28 | 12 | 80 | D1 | 00 00 00 | 42 |
| Printed, every tick | 28 | 12 | 80 | D1 | 00 00 00 | 42, CRC FAIL |

That decides which commands can stay broadcast (Skip ROM) and which need an address (Match ROM):

| Command | Reply | With Skip ROM and two probes |
|---|---|---|
| Convert T (`44h`) | none | OK: both convert at once |
| Read Power Supply (`B4h`) | 1 bit | OK: a 1 means *every* probe is externally powered |
| Read ROM (`33h`) | 8 bytes | Collides (above); replaced by the stored IDs |
| Read Scratchpad (`BEh`) | 9 bytes | Collides; sent after Match ROM instead |

During Match ROM, each probe compares the ID bits as they arrive and drops out at the first mismatch. Probe 2 drops out at the 14th bit of probe 1's ID (the first place the two IDs differ), so only probe 1 answers the Read Scratchpad that follows.

### Sampling and timing

A repeating hardware timer (`add_repeating_timer_ms(-1000, …)`; negative = interval measured from callback start, so it doesn't drift) sets a `volatile` flag. The main loop clears the flag *before* doing the work, so a tick that arrives mid-work isn't lost.

### Flash logging

Not built yet.

## Results so far

| Measurement | Target or reference | Result |
|---|---|---|
| Sample interval | 1 s | 1 s hardware timer |
| Loop latency (board clock) | — | 299 of 300 gaps exactly 1,000,000 µs; the only off gap is the first one after the laptop opens the port (3–5 µs short, seen in 4 of 4 runs) |
| Timing jitter (oscilloscope) | — | Pending |
| Sensor conversion time | 750 ms max at 12-bit (datasheet) | Not measurable on these probes (see below); fixed 750 ms wait used |
| Temperature resolution | 0.0625 °C (12-bit) | Confirmed: readings step 22.1875 → 22.2500 °C |
| Built-in sensor resolution | — | ~0.47 °C per ADC count (3.3 V / 4096 ≈ 0.81 mV) |
| Probe-to-probe difference, indoors | ≤ 1.0 °C (two ±0.5 °C probes) | Tips taped together, 5 min untouched, 16 consecutive readings: identical in 11, one count (0.0625 °C) apart in 5, never more. CRC passed 16/16 on both |
| Accuracy vs. a reference thermometer | ±0.5 °C | Open: probes read 2–3 °C below a food thermometer and a desk clock, but they weren't side by side. Retest co-located |
| Outdoor reading vs. NWS station | — | Pending |
| Current draw | — | Pending |
| Storage capacity | — | Pending |
| Records captured vs. expected | 100% | Pending |

The loop-latency numbers come from the board's own clock, which also drives the timer, so they show how quickly the loop notices each tick, not how accurate the timer is. The oscilloscope measurement will check that independently.

The two probes agreeing doesn't show that either one is accurate: they could share the same offset. The one-count flicker is rounding: near a count boundary, each probe rounds on its own.

## What broke and how I fixed it

- **Conversion-time polling returned 0.0 ms.** The datasheet says an externally powered DS18B20 answers 0 to read slots while converting and 1 when done. I predicted 500–700 ms; every poll returned 1 immediately. I tested three explanations in order:
  1. *Parasite power:* adapter VCC measured 3.284 V, and Read Power Supply (`B4h`) reported external power → ruled out.
  2. *Conversion never starting:* after a fixed 750 ms wait the scratchpad held a real temperature (not the 85.0 °C power-on default) → ruled out.
  3. *Probe doesn't signal busy:* the remaining explanation. I couldn't confirm genuine vs. clone chip from the scratchpad bytes. I switched to a fixed 750 ms wait, which works either way.
- **Internal pull resistors instead of driving the pin.** I first used `gpio_pull_down`/`gpio_pull_up`, which only enable weak (~50 kΩ) internal resistors that lose to the 4.7 kΩ pull-up. Fixed by switching pin direction instead.
- **Physical pin vs. GPIO number.** The data wire is on physical pin 4, which is GPIO 2. The SDK uses GPIO numbers. Fixed with `#define ONEWIRE_PIN 2`.
- **Inverted presence logic.** `gpio_get` returns the line level, and on 1-Wire low means "present."
- **Wiring caught before power-up.** A photo check showed the ground jumper on VSYS/VBUS (5 V) instead of GND, which would have reverse-powered the probe. Moved before plugging in, then checked 3V3–GND for continuity.
- **Skip ROM with two probes.** Read ROM and Read Scratchpad collided into the AND of both probes (see "Two probes on one wire"). Fixed with Match ROM and the stored IDs.
- **Pointer to a row vs. pointer to a byte.** I passed `probe_rom` (the whole 2×8 table) where the function expects `probe_rom[p]` (one 8-byte ID). Both start at the same address, so it would have read probe 1 correctly by accident; the compiler's incompatible-pointer warning caught it.
- **Difference printed from stale data.** I first checked `temp_c[0] && temp_c[1]` ("both nonzero") instead of whether both reads passed. A failed read would have reused last second's value, and a real 0.0 °C reading would have counted as missing. Fixed with per-probe `ok` flags.
- **`printf` format mismatches.** `%d` with a `float` and a missing `uint64_t` placeholder printed garbage; fixed with `%.2f` and `PRIu64`.

## Building and running

- Raspberry Pi Pico VS Code extension, Pico SDK 2.3.1, board `pico2`, Arm. Build with **Compile** and flash with **Run** in the extension's status bar.
- Output is printed over USB serial (115200 baud setting is ignored over USB).
- `tools/jitter.py` needs `pyserial` (`python3 -m pip install pyserial`). The serial port name is set at the top of the script; change it to match your board, close any serial monitor, then run `python3 tools/jitter.py`.

## Credits

- Project scaffold generated by the Raspberry Pi Pico VS Code extension; uses the Pico SDK (`pico_stdlib`, `hardware_adc`, `hardware_sync`).
- 1-Wire timing, commands, and the CRC-8 algorithm come from the [DS18B20 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf). Built-in sensor conversion constants (0.706 V at 27 °C, −1.721 mV/°C) come from the RP2350 datasheet.
- I wrote the code myself, working step by step with an AI tutor (Claude) for explanations and code review. This README was drafted with AI assistance from my project log.
