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

int main() {
    static uint16_t raw_measurement;
    float volts, temp;
    repeating_timer_t timer;
    uint64_t t_us;
    uint8_t rom[8];

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
