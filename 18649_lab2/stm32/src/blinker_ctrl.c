#include <zephyr/kernel.h>
#include <stdbool.h>
#include "blinker.h"
#include "blinker_ctrl.h"

/* TODO: tune and document — steering units match your UART command frame's
 * ±1000 range. This is the point past which turning the wheel counts as
 * "committed to the turn" for self-cancel purposes. */
#define TURN_THRESHOLD_LEFT   (-400)
#define TURN_THRESHOLD_RIGHT  (400)

#define BLINK_PERIOD_MS   1000   /* 1Hz nominal */
#define BLINK_ON_MS       (BLINK_PERIOD_MS / 2)   /* 50% duty */
#define HAZARD_PERIOD_MS  500    /* 2Hz nominal, per S4 hazard spec */
#define HAZARD_ON_MS      (HAZARD_PERIOD_MS / 2)

static enum blink_state state = BLINK_OFF;
static bool past_threshold = false;

K_MUTEX_DEFINE(state_lock);

void blinker_signal_left_pressed(void)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    if (state == BLINK_LEFT) {
        state = BLINK_OFF;
    } else if (state != BLINK_HAZARD) {
        state = BLINK_LEFT;
        past_threshold = false;
    }
    k_mutex_unlock(&state_lock);
}

void blinker_signal_right_pressed(void)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    if (state == BLINK_RIGHT) {
        state = BLINK_OFF;
    } else if (state != BLINK_HAZARD) {
        state = BLINK_RIGHT;
        past_threshold = false;
    }
    k_mutex_unlock(&state_lock);
}

void blinker_update_steering(int16_t steering)
{
    k_mutex_lock(&state_lock, K_FOREVER);

    if (state == BLINK_LEFT) {
        if (steering <= TURN_THRESHOLD_LEFT) {
            past_threshold = true;
        } else if (past_threshold && steering > TURN_THRESHOLD_LEFT) {
            /* crossed past threshold, now returned past it -> self-cancel */
            state = BLINK_OFF;
        }
    } else if (state == BLINK_RIGHT) {
        if (steering >= TURN_THRESHOLD_RIGHT) {
            past_threshold = true;
        } else if (past_threshold && steering < TURN_THRESHOLD_RIGHT) {
            state = BLINK_OFF;
        }
    }

    k_mutex_unlock(&state_lock);
}

void blinker_set_hazard(bool on)
{
    k_mutex_lock(&state_lock, K_FOREVER);
    state = on ? BLINK_HAZARD : BLINK_OFF;
    k_mutex_unlock(&state_lock);
}

static void blinker_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    bool light_on = false;

    while (1) {
        enum blink_state current;

        k_mutex_lock(&state_lock, K_FOREVER);
        current = state;
        k_mutex_unlock(&state_lock);

        int sleep_duration_ms;
        int period_ms = (current == BLINK_HAZARD) ? HAZARD_PERIOD_MS : BLINK_PERIOD_MS;
        int on_ms     = (current == BLINK_HAZARD) ? HAZARD_ON_MS     : BLINK_ON_MS;

        switch (current) {
        case BLINK_LEFT:
            blinker_set(BLINKER_FL, light_on);
            blinker_set(BLINKER_RL, light_on);
            blinker_set(BLINKER_FR, false);
            blinker_set(BLINKER_RR, false);
            break;

        case BLINK_RIGHT:
            blinker_set(BLINKER_FR, light_on);
            blinker_set(BLINKER_RR, light_on);
            blinker_set(BLINKER_FL, false);
            blinker_set(BLINKER_RL, false);
            break;

        case BLINK_HAZARD:
            blinker_set(BLINKER_FL, light_on);
            blinker_set(BLINKER_FR, light_on);
            blinker_set(BLINKER_RL, light_on);
            blinker_set(BLINKER_RR, light_on);
            break;

        case BLINK_OFF:
        default:
            blinker_set(BLINKER_FL, false);
            blinker_set(BLINKER_FR, false);
            blinker_set(BLINKER_RL, false);
            blinker_set(BLINKER_RR, false);
            light_on = false;   /* force next iteration to start from OFF */
            break;
        }

        /* Decide how long to stay in the state we just displayed */
        sleep_duration_ms = light_on ? on_ms : (period_ms - on_ms);

        /* Flip for next iteration */
        light_on = !light_on;

        k_sleep(K_MSEC(sleep_duration_ms));
    }
}

K_THREAD_DEFINE(blinker_tid, 512, blinker_thread_fn, NULL, NULL, NULL, 6, 0, 0);

void blinker_control_start(void)
{
    /* Thread already started via K_THREAD_DEFINE; this function exists
     * as an explicit hook in case init ordering ever needs it. */
}