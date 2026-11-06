#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"


int main() {
    static uint16_t raw_measurement;
    float volts, temp;

    stdio_init_all();
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    while (true) {
        raw_measurement = adc_read();
        volts = raw_measurement * 3.3 / 4096.0;
        temp = 27 - (volts - 0.706) / 0.001721;
        printf("Temperature is: %.2f C°| Volts: %.4f\n", temp, volts);
        sleep_ms(1000);
    }
}
