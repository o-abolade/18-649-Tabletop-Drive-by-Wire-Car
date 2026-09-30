#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "pi_stm32_uart.h"
#include "blinker.h"
#include "blinker_ctrl.h"
#include "steering.h"
#include "current.h"

#define STATE_INIT     0
#define STATE_NORMAL   1
#define STATE_FAILSAFE 2
#define STATE_SELFTEST 3

#define FAILSAFE_TIMEOUT_MS 150

#define LEFT_TURN_BIT  0x01
#define RIGHT_TURN_BIT 0x02
#define SELF_TEST_BIT  0x04

static atomic_t zone_state = ATOMIC_INIT(STATE_INIT);

/* Read one sensor as a raw ADC count; report 0 on read failure. */
static uint16_t read_current_raw(enum current_id id)
{
    int raw = current_read_raw(id);
    return (raw < 0) ? 0 : (uint16_t)raw;
}

// Status heartbeat thread: sends every 20ms
static void status_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (1) {
        status_frame_t st;
        st.state = (uint8_t)atomic_get(&zone_state);

        /* Raw 12-bit ADC counts (0..4095). The Pi converts to amps using
         * the formula in current_raw_to_amps(); document this in the
         * status frame spec. */
        st.motor1_current = read_current_raw(CURRENT_MOTOR_A);
        st.motor2_current = read_current_raw(CURRENT_MOTOR_B);
        st.servo_current  = read_current_raw(CURRENT_SERVO);

        cmd_frame_t last_cmd;
        st.seq = pi_stm32_uart_get_latest_cmd(&last_cmd) ? last_cmd.seq : 0;

        pi_stm32_uart_send_status(&st);
        k_sleep(K_MSEC(20));
    }
}
K_THREAD_DEFINE(status_tid, 1024, status_thread_fn, NULL, NULL, NULL, 7, 0, 0);

// Control thread: reads latest command, checks failsafe, drives actuators
static void control_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    uint8_t prev_buttons = 0;

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
            // TODO 3.1/3.2: disable motor PWM, engage H-bridge dynamic braking.
            // Note: do NOT call servo_disable() here. R3 says steering keeps
            // tracking in the error state; with no link, it holds its last angle.

        } else {
            if (atomic_get(&zone_state) == STATE_FAILSAFE) {
                printk("*** RECOVERED TO NORMAL ***\n");
                blinker_set_hazard(false);
            }
            atomic_set(&zone_state, STATE_NORMAL);

            printk("seq=%u steer=%d thr=%u brk=%u btn=0x%02x age=%ums\n",
                   cmd.seq, cmd.steering, cmd.throttle, cmd.brake,
                   cmd.buttons, age);

            // Steering
            set_wheel_angle(cmd.steering);

            // TODO 3.1/3.2: apply cmd.throttle/brake to the motors.

            // Blinkers: act on rising edges of the turn buttons
            bool left_now   = cmd.buttons & LEFT_TURN_BIT;
            bool right_now  = cmd.buttons & RIGHT_TURN_BIT;
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
    int ret;

    pi_stm32_uart_init();

    if ((ret = blinker_init_all()) < 0) {
        printk("blinker_init_all failed: %d\n", ret);
    }
    if ((ret = servo_init()) < 0) {
        printk("servo_init failed: %d\n", ret);
    }
    if ((ret = current_init()) < 0) {
        printk("current_init failed: %d\n", ret);
    }

    printk("Lab 2 STM32 online. Waiting for commands...\n");
    return 0;
}