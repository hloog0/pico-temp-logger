#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <inttypes.h>

#define ONEWIRE_PIN 2

volatile bool perform_sample = false;

bool timer_callback(repeating_timer_t * rt) {
    perform_sample = true;
    return true;
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

int main() {
    static uint16_t raw_measurement;
    float volts, temp;
    repeating_timer_t timer;
    uint64_t t_us;

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
            if (onewire_reset(ONEWIRE_PIN)) {
                printf("presence detected\n");
            } else {
                printf("no device\n");
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
