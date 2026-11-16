# pico-temp-logger

A temperature logger on a Raspberry Pi Pico 2 (RP2350) using DS18B20 digital temperature probes. I wrote the 1-Wire driver myself in C from the DS18B20 datasheet instead of using a library.

**Status: in progress.** One probe reads CRC-checked temperatures through my own driver. Next: two probes on one wire, logging to onboard flash, and a week-long outdoor run. Dated progress is in [LOG.md](LOG.md).

## What works now

- **1-Wire driver** (bit-banged on one GPIO): reset/presence detection, read and write time slots with interrupts disabled only during the timing-critical part of each slot, byte read/write (LSB first), and Dallas/Maxim CRC-8.
- **DS18B20 commands:** Read ROM (`33h`), Skip ROM (`CCh`), Convert T (`44h`), Read Scratchpad (`BEh`), Read Power Supply (`B4h`).
- **Temperature readout:** 12-bit resolution (0.0625 °C steps), every reading checked with CRC-8.
- **Timer-driven sampling:** a hardware repeating timer sets a flag once per second; the main loop does the work.
- **Built-in sensor:** the RP2350's internal temperature sensor read through the ADC, for comparison.
- **`tools/jitter.py`:** a Python script that reads timestamps over USB serial and reports sample-interval statistics.

## Hardware

- Raspberry Pi Pico 2 (non-W), pre-soldered headers
- 2× DROK waterproof DS18B20 probes, each with a DROK adapter board (screw terminal + 4.7 kΩ pull-up between DAT and VCC)
- Female–female jumper wires, USB-A to micro-USB cable

## Wiring

Probe 1 (external power mode, 3.3 V):

| Probe wire | Adapter terminal | Jumper color | Pico 2 pin |
|---|---|---|---|
| Red | VCC | Orange | 3V3(OUT), physical pin 36 |
| Black | GND | Green | GND, physical pin 38 |
| Yellow | DAT | Yellow | GP2 (GPIO 2), physical pin 4 |

The adapter's 4.7 kΩ resistor pulls the data line up to 3.3 V. The firmware also enables the RP2350's weak internal pull-up as a backup, so a disconnected data wire reads as "no device" instead of floating.

## How it works

The code is built in layers; each layer only uses the one below it.

```
DS18B20 commands   ds18b20_start_conversion, ds18b20_read_scratchpad, ...
1-Wire bytes       onewire_write_byte / onewire_read_byte (LSB first), onewire_crc8
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

Interrupts are turned off from the start of each slot until its release (write) or sample (read), so a USB or timer interrupt can't push a sample past its window. They stay on during the long waits, so they're held off for at most ~85 µs.

### Reading a temperature

1. Reset → Skip ROM → Convert T
2. Wait 750 ms (the datasheet maximum at 12-bit; see "What broke" for why it's a fixed wait)
3. Reset → Skip ROM → Read Scratchpad → 9 bytes → check CRC-8
4. Bytes 0–1 form a signed 16-bit value (low byte first); divide by 16 for °C. Signed matters below 0 °C, where the probe sends two's-complement negatives.

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
| Probe-to-probe difference, indoors | — | Pending |
| Outdoor reading vs. NWS station | — | Pending |
| Current draw | — | Pending |
| Storage capacity | — | Pending |
| Records captured vs. expected | 100% | Pending |

The loop-latency numbers come from the board's own clock, which also drives the timer, so they show how quickly the loop notices each tick, not how accurate the timer is. The oscilloscope measurement will check that independently.

## What broke and how I fixed it

- **Conversion-time polling returned 0.0 ms.** The datasheet says an externally powered DS18B20 answers 0 to read slots while converting and 1 when done. I predicted 500–700 ms; every poll returned 1 immediately. I tested three explanations in order:
  1. *Parasite power:* adapter VCC measured 3.284 V, and Read Power Supply (`B4h`) reported external power → ruled out.
  2. *Conversion never starting:* after a fixed 750 ms wait the scratchpad held a real temperature (not the 85.0 °C power-on default) → ruled out.
  3. *Probe doesn't signal busy:* the remaining explanation. I couldn't confirm genuine vs. clone chip from the scratchpad bytes. I switched to a fixed 750 ms wait, which works either way.
- **Internal pull resistors instead of driving the pin.** I first used `gpio_pull_down`/`gpio_pull_up`, which only enable weak (~50 kΩ) internal resistors that lose to the 4.7 kΩ pull-up. Fixed by switching pin direction instead.
- **Physical pin vs. GPIO number.** The data wire is on physical pin 4, which is GPIO 2. The SDK uses GPIO numbers. Fixed with `#define ONEWIRE_PIN 2`.
- **Inverted presence logic.** `gpio_get` returns the line level, and on 1-Wire low means "present."
- **Wiring caught before power-up.** A photo check showed the ground jumper on VSYS/VBUS (5 V) instead of GND, which would have reverse-powered the probe. Moved before plugging in, then checked 3V3–GND for continuity.
- **`printf` format mismatches.** `%d` with a `float` and a missing `uint64_t` placeholder printed garbage; fixed with `%.2f` and `PRIu64`.

## Building and running

- Raspberry Pi Pico VS Code extension, Pico SDK 2.3.1, board `pico2`, Arm. Build with **Compile** and flash with **Run** in the extension's status bar.
- Output is printed over USB serial (115200 baud setting is ignored over USB).
- `tools/jitter.py` needs `pyserial` (`python3 -m pip install pyserial`). The serial port name is set at the top of the script; change it to match your board, close any serial monitor, then run `python3 tools/jitter.py`.

## Credits

- Project scaffold generated by the Raspberry Pi Pico VS Code extension; uses the Pico SDK (`pico_stdlib`, `hardware_adc`, `hardware_sync`).
- 1-Wire timing, commands, and the CRC-8 algorithm come from the [DS18B20 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf). Built-in sensor conversion constants (0.706 V at 27 °C, −1.721 mV/°C) come from the RP2350 datasheet.
- I wrote the code myself, working step by step with an AI tutor (Claude) for explanations and code review. This README was drafted with AI assistance from my project log.
