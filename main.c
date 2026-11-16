#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/sync.h"
#include <inttypes.h>

#define ONEWIRE_PIN 2

volatile bool perform_sample = false;

bool timer_callback(repeating_timer_t * rt) {
    perform_sample = true;
    return true;
}

void onewire_write_bit(uint pin, bool bit) {
    if (bit) { // bit = 1
        uint32_t irq = save_and_disable_interrupts();
        gpio_set_dir(pin, GPIO_OUT);
        busy_wait_us(5);
        gpio_set_dir(pin, GPIO_IN);
        restore_interrupts(irq);
        busy_wait_us(60);
    } else {
        uint32_t irq = save_and_disable_interrupts();
        gpio_set_dir(pin, GPIO_OUT);
        busy_wait_us(80);
        gpio_set_dir(pin, GPIO_IN);
        restore_interrupts(irq);
        busy_wait_us(5);
    }
}

bool onewire_read_bit(uint pin) {
    bool pin_read;
    uint32_t irq = save_and_disable_interrupts();
    gpio_set_dir(pin, GPIO_OUT);
    busy_wait_us(3);
    gpio_set_dir(pin, GPIO_IN);
    busy_wait_us(7);
    pin_read = gpio_get(pin);
    restore_interrupts(irq);
    busy_wait_us(50);
    return pin_read;
}

bool onewire_reset(uint pin) {
    bool line_high;
    gpio_set_dir(pin, GPIO_OUT);
    busy_wait_us(480);
    gpio_set_dir(pin, GPIO_IN);
    busy_wait_us(70);
    line_high = gpio_get(pin); // gpio_get() returns 0 if low
    busy_wait_us(410);
    return !line_high; // the probe is present (1) if low (0)
}

void onewire_write_byte(uint pin, uint8_t byte) {
    bool bit;
    for (int i = 0; i < 8; i++) {
        // shift byte so that target bit is at 0th bit, then mask with 0000 0001 using & so that 0's stay 0, 1's stay 1
        bit = (byte >> i) & 1;
        onewire_write_bit(pin, bit);
    }
}

uint8_t onewire_read_byte(uint pin) {
    uint8_t result = 0;
    for (int i = 0; i < 8; i++) {
        if (onewire_read_bit(pin)) { // read a 1
            // 0 and 1's of non target bits stay as is, target bit 0->1
            result |= 1 << i;
        }
    }

    return result;
}

uint8_t onewire_crc8(const uint8_t *data, int len) {
    uint8_t crc = 0;
    uint8_t mix;
    for (int i = 0; i < len; i++) {
        for (int j = 0; j < 8; j++) {
            mix = (crc & 1) ^ ((data[i] >> j) & 1);
            crc >>= 1;
            if (mix == 1) {
                crc ^= 0x8C;
            }
        }
    }

    return crc;
}

bool ds18b20_start_conversion(uint pin) {
    if (!onewire_reset(pin)) {
        return false;
    }

    onewire_write_byte(pin, 0xCC);
    onewire_write_byte(pin, 0x44);

    return true;
}

uint32_t ds18b20_wait_conversion(uint pin) {
    uint64_t start_time = time_us_64();
    uint64_t elapsed_time = 0;
    while (onewire_read_bit(pin) != 1) {
        elapsed_time = time_us_64() - start_time;
        if (elapsed_time >= 1000000) {
            return UINT32_MAX;
        }
    }

    return elapsed_time;
}

bool ds18b20_is_ext_powered(uint pin) {
    if (!onewire_reset(pin)) {
        return false;
    }

    onewire_write_byte(pin, 0xCC);
    onewire_write_byte(pin, 0xB4);

    return onewire_read_bit(pin);
}

bool ds18b20_read_scratchpad(uint pin, uint8_t *scratch) {
    if (!onewire_reset(pin)) {
        return false;
    }
    onewire_write_byte(pin, 0xCC);
    onewire_write_byte(pin, 0xBE);
    for (int i = 0; i < 9; i++) {
        scratch[i] = onewire_read_byte(pin);
    }

    if (onewire_crc8(scratch, 9) == 0) {
        return true;
    } else {
        return false;
    }
}

float ds18b20_raw_to_celcius(const uint8_t *scratch) {
    int16_t raw = (scratch[1] << 8) | scratch[0];
    return raw / 16.0f;
}

int main() {
    static uint16_t raw_measurement;
    float volts, temp;
    repeating_timer_t timer;
    uint64_t t_us;
    uint8_t rom[8], scratch[9];
    uint32_t ds18b20_elapsed_time;

    gpio_init(ONEWIRE_PIN);
    gpio_pull_up(ONEWIRE_PIN);
    gpio_put(ONEWIRE_PIN, 0);

    stdio_init_all();
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    // timer
    add_repeating_timer_ms(-1000, timer_callback, NULL, &timer);

    // sample loop
    while (true) {
        if (perform_sample) {
            perform_sample = false;
            if (!onewire_reset(ONEWIRE_PIN)) {
                printf("!! no device !!\n");
            } else {
                onewire_write_byte(ONEWIRE_PIN, 0x33);
                for (int i = 0; i < 8; i++) {
                    rom[i] = onewire_read_byte(ONEWIRE_PIN);
                }
                for (int i = 0; i < 8; i++) {
                    printf("%02X ", rom[i]);
                }
                printf("\n");
                
                if (onewire_crc8(rom, 7) == rom[7]) {
                    printf("CRC OK\n");
                } else {
                    printf("CRC FAIL\n");
                }

                if (ds18b20_is_ext_powered(ONEWIRE_PIN)) {
                    printf("externally powered\n");
                } else {
                    printf("parasite powered\n");
                }

                if (ds18b20_start_conversion(ONEWIRE_PIN)) {
                    sleep_ms(750);
                    if (ds18b20_read_scratchpad(ONEWIRE_PIN, scratch)) {
                        for (int i = 0; i < 9; i++) {
                            printf("%02X ", scratch[i]);
                        }

                        printf("\n");

                        if (scratch[0] == 0x50 && scratch[1] == 0x05) {
                            printf("conversion did NOT happen\n");
                        } else {
                            printf("conversion happened\n");
                            printf("%.4f °C\n", ds18b20_raw_to_celcius(scratch));
                        }
                    } else {
                        printf("scratchpad CRC FAIL or no device\n");
                    }
                    
                    /* 
                    ds18b20_elapsed_time = ds18b20_wait_conversion(ONEWIRE_PIN);
                    if (ds18b20_elapsed_time == UINT32_MAX) {
                        printf("conversion timed out\n");
                    } else {
                    
                        printf("%.1f ms\n", ds18b20_elapsed_time / 1000.0f);
                    }
                    */

                } else {
                    printf("no device\n");
                }
            }

            /* 
            t_us = time_us_64();
            raw_measurement = adc_read();
            volts = raw_measurement * 3.3 / 4096.0;
            temp = 27 - (volts - 0.706) / 0.001721;
            printf("%" PRIu64 ",%.2f,%.4f\n", t_us, temp, volts); 
            */
            // printf("Temperature is: %.2f C°| Volts: %.4f\n", temp, volts);
        }
    }
}
