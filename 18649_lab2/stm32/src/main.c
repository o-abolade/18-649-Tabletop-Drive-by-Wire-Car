#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "pi_stm32_uart.h"
#include "blinker.h"

#define SLEEP_LED_TIME_MS   400

#define STATE_INIT     0
#define STATE_NORMAL   1
#define STATE_FAILSAFE 2
#define STATE_SELFTEST 3

#define FAILSAFE_TIMEOUT_MS 150

static atomic_t zone_state = ATOMIC_INIT(STATE_INIT);

/*
 * Part 3 implementation plan (pseudocode only -- no motor pins are driven yet)
 *
 * encoder_init():
 *   configure LEFT_A, LEFT_B, RIGHT_A, RIGHT_B as GPIO interrupt inputs
 *   initialize atomic encoder counts to zero
 *
 * encoder ISR for each A/B edge:
 *   read both signals
 *   use the previous and current A/B state to add +1 or -1 to that wheel count
 *   do no printing, sleeping, or control calculation in the ISR
 *
 * every CONTROL_PERIOD_MS:
 *   left_delta  = atomic exchange(left_count_since_last_sample, 0)
 *   right_delta = atomic exchange(right_count_since_last_sample, 0)
 *   left_speed/right_speed = delta / CONTROL_PERIOD_MS
 *   vehicle_speed = (left_speed + right_speed) / 2
 *
 *   if command is stale OR brake is active:
 *       PWM = 0; H-bridge = dynamic braking; hazards = on
 *   else:
 *       target_speed = monotonic_map(throttle, 0..1000, 0..MAX_SPEED)
 *       error = target_speed - vehicle_speed
 *       pwm_request = clamp(KP * error + optional_integral + optional_derivative,
 *                           0, MAX_PWM)
 *       write PWM and the forward direction pins to the L298N
 *
 * Pin names, voltage checks, encoder polarity, PWM frequency, MAX_SPEED, MAX_PWM,
 * and gains remain intentionally unassigned until the physical wiring is verified.
 */

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
            }
            atomic_set(&zone_state, STATE_FAILSAFE);

            // TODO Part 3: disable motor PWM, engage H-bridge dynamic braking, drive all four blinkers into the hazard pattern.

        } else {
            if (atomic_get(&zone_state) == STATE_FAILSAFE) {
                printk("*** RECOVERED TO NORMAL ***\n");
            }
            atomic_set(&zone_state, STATE_NORMAL);

            printk("seq=%u steer=%d thr=%u brk=%u btn=0x%02x age=%ums\n",
                   cmd.seq, cmd.steering, cmd.throttle, cmd.brake,
                   cmd.buttons, age);

            // TODO Part 3: apply cmd.steering/throttle/brake/buttons to the actual actuators.
        }

        k_sleep(K_MSEC(10));
    }
}
K_THREAD_DEFINE(control_tid, 1024, control_thread_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    pi_stm32_uart_init();
    printk("Lab 2 STM32 online. Waiting for commands...\n");

    // printk("lab2 up\n");
    
	// bool led_state = true;

    // blinker_init(BLINKER_FL);
    // while (1) {
    //     blinker_set(BLINKER_FL, led_state);
    //     printk("Toggling LED, in test mode\n");
	// 	led_state = !led_state;
	// 	k_msleep(SLEEP_TIME_MS);
	// }
    
    return 0;
}
