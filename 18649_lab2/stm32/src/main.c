#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "pi_stm32_uart.h"
#include "blinker.h"
#include "blinker_ctrl.h"

#define SLEEP_LED_TIME_MS   400

#define STATE_INIT     0
#define STATE_NORMAL   1
#define STATE_FAILSAFE 2
#define STATE_SELFTEST 3

#define FAILSAFE_TIMEOUT_MS 150

#define LEFT_TURN_BIT  0x01
#define RIGHT_TURN_BIT 0x02
#define SELF_TEST_BIT  0x04

static atomic_t zone_state = ATOMIC_INIT(STATE_INIT);

// Status heartbeat thread: sends every 20ms
static void status_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (1) {
        status_frame_t st;
        st.state = (uint8_t)atomic_get(&zone_state);

        // TODO Part 3: replace with real ADC current-sensor readings
        st.motor1_current = 0;
        st.motor2_current = 0;
        st.servo_current  = 0;

        cmd_frame_t last_cmd;
        if (pi_stm32_uart_get_latest_cmd(&last_cmd)) {
            st.seq = last_cmd.seq;
        } else {
            st.seq = 0;   /* no command ever received yet */
        }

        pi_stm32_uart_send_status(&st);
        k_sleep(K_MSEC(20));
    }
}
K_THREAD_DEFINE(status_tid, 512, status_thread_fn, NULL, NULL, NULL, 7, 0, 0);

// Control thread: reads latest command, checks failsafe
static void control_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (1) {
        cmd_frame_t cmd;
        bool have = pi_stm32_uart_get_latest_cmd(&cmd);
        uint32_t age = pi_stm32_uart_ms_since_last_cmd();

        if (!have || age > FAILSAFE_TIMEOUT_MS) {
            if (atomic_get(&zone_state) != STATE_FAILSAFE) {
                printk("*** ENTERING FAILSAFE (age=%ums) ***\n", age);
                blinker_set_hazard(true);
            }
            atomic_set(&zone_state, STATE_FAILSAFE);
        // TODO Part 3: disable motor PWM, engage H-bridge dynamic braking, drive all four blinkers into the hazard pattern.
        } else {
            if (atomic_get(&zone_state) == STATE_FAILSAFE) {
                printk("*** RECOVERED TO NORMAL ***\n");
                blinker_set_hazard(false);
            }
            atomic_set(&zone_state, STATE_NORMAL);

            printk("seq=%u steer=%d thr=%u brk=%u btn=0x%02x age=%ums\n",
                   cmd.seq, cmd.steering, cmd.throttle, cmd.brake,
                   cmd.buttons, age);

            // TODO Part 3: apply cmd.steering/throttle/brake/buttons to the actual actuators.
            static uint8_t prev_buttons = 0;

            // For sterring/throttle/brake to actual actuators

            

            // For blinker control, detect button presses (rising edges) and calls the appropriate blinker_ctrl functions.
            bool left_now  = cmd.buttons & LEFT_TURN_BIT;
            bool right_now = cmd.buttons & RIGHT_TURN_BIT;
            bool left_prev  = prev_buttons & LEFT_TURN_BIT;
            bool right_prev = prev_buttons & RIGHT_TURN_BIT;

            if (left_now && !left_prev) {
                blinker_signal_left_pressed();
            }
            if (right_now && !right_prev) {
                blinker_signal_right_pressed();
            }
            prev_buttons = cmd.buttons;

            blinker_update_steering(cmd.steering);
        }

        k_sleep(K_MSEC(10));
    }
}
K_THREAD_DEFINE(control_tid, 1024, control_thread_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    pi_stm32_uart_init();
    blinker_init_all();
    printk("Lab 2 STM32 online. Waiting for commands...\n");
    
    return 0;
}