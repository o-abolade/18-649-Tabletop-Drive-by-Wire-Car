#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/drivers/gpio.h>
#include "blinker.h"

#define SLEEP_TIME_MS   400



int main(void)
{
    printk("lab2 up\n");
    
	bool led_state = true;

    blinker_init(BLINKER_FL);




    while (1) {
        blinker_set(BLINKER_FL, led_state);
        printk("Toggling LED, in test mode\n");
		led_state = !led_state;
		k_msleep(SLEEP_TIME_MS);
	}

    return 0;
}
