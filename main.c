#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include <inttypes.h>

volatile bool perform_sample = false;

bool timer_callback(repeating_timer_t * rt) {
    perform_sample = true;
    return true;
}

int main() {
    static uint16_t raw_measurement;
    float volts, temp;
    repeating_timer_t timer;
    uint64_t t_us;

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
            t_us = time_us_64();
            raw_measurement = adc_read();
            volts = raw_measurement * 3.3 / 4096.0;
            temp = 27 - (volts - 0.706) / 0.001721;
            printf("%" PRIu64 ",%.2f,%.4f\n", t_us, temp, volts);
            // printf("Temperature is: %.2f C°| Volts: %.4f\n", temp, volts);
        }
    }
}
