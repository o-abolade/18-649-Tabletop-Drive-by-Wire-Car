/*
 * pi_bridge.c — UDP-to-serial bridge between the wheel proxy and the STM32.
 *
 * Reuses the DIJOYSTATE2_t parsing approach from receiver.c (Appendix A).
 * Builds/sends 13-byte command frames to the STM32 at least every 50ms,
 * and listens for/prints 13-byte status frames coming back.
 *
 * Build:  gcc -O2 -Wall -o pi_bridge pi_bridge.c
 * Run:    ./pi_bridge <serial-device> [--invalid-test]
 *         e.g. ./pi_bridge /dev/ttyAMA0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <termios.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "state.h"   // DIJOYSTATE2_t definition, from receiver.c's setup

// Protocol constants (must match STM32 side exactly)
#define SYNC0        0xAA
#define SYNC1        0x55
#define TYPE_CMD     0x01
#define TYPE_STATUS  0x02
#define FRAME_LEN    13
#define UDP_PORT     8000
#define SEND_PERIOD_MS 50

#define STEER_RAW_LEFT     (-32768)
#define STEER_RAW_CENTER   (0)
#define STEER_RAW_RIGHT    (32767)

#define THROTTLE_RAW_REST (32767)
#define THROTTLE_RAW_MAX  (-32768)

#define BRAKE_RAW_REST    (32767)
#define BRAKE_RAW_MAX     (-32768)

#define BTN_RIGHT_TURN_SIG  4
#define BTN_LEFT_TURN_SIG   5
#define BTN_SELF_TEST_SIG   6

// CRC16-CCITT, must match Zephyr's crc16_ccitt(0xFFFF, ...) 
static uint16_t crc16_ccitt(uint16_t seed, const uint8_t *src, size_t len)
{
    for (; len > 0; len--) {
        uint8_t e, f;

        e = seed ^ *src;
        ++src;
        f = e ^ (e << 4);
        seed = (seed >> 8) ^ ((uint16_t)f << 8) ^ ((uint16_t)f << 3) ^ ((uint16_t)f >> 4);
    }

    return seed;
}

// Serial setup
static int open_serial(const char *dev)
{
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open serial");
        exit(1);
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        exit(1);
    }

    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;   // 8 data bits
    tty.c_cflag &= ~PARENB;                        // no parity
    tty.c_cflag &= ~CSTOPB;                        // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                       // no hw flow control
    tty.c_cflag |= (CLOCAL | CREAD);

    tty.c_lflag = 0;      // raw input, no canonical mode/echo/signals
    tty.c_oflag = 0;      // raw output
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(ICRNL | INLCR);

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        exit(1);
    }

    return fd;
}

// UDP setup
static int open_udp(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(UDP_PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("Listening for wheel UDP packets on port %d\n", UDP_PORT);
    return fd;
}

// Scaling helpers
static int16_t scale_steering(long raw)
{
    // Map [STEER_RAW_LEFT, STEER_RAW_RIGHT] -> [-1000, 1000], monotonic.
    long span = STEER_RAW_RIGHT - STEER_RAW_LEFT;
    long shifted = raw - STEER_RAW_LEFT;
    long scaled = (shifted * 2000) / span - 1000;
    if (scaled < -1000) scaled = -1000;
    if (scaled > 1000) scaled = 1000;
    return (int16_t)scaled;
}

static uint16_t scale_pedal(long raw, long rest, long max)
{
    long span = max - rest;
    long shifted = raw - rest;
    long scaled = (shifted * 1000) / span;
    if (scaled < 0) scaled = 0;
    if (scaled > 1000) scaled = 1000;
    return (uint16_t)scaled;
}

static bool button_pressed(const DIJOYSTATE2_t *js, int idx)
{
    return js->rgbButtons[idx] & 0x80;
}

// Command frame builder
static void build_command_frame(uint8_t *buf, int16_t steering, uint16_t throttle,
                                 uint16_t brake, uint8_t buttons, uint8_t seq)
{
    buf[0] = SYNC0;
    buf[1] = SYNC1;
    buf[2] = TYPE_CMD;
    buf[3] = seq;
    buf[4] = steering & 0xFF;
    buf[5] = (steering >> 8) & 0xFF;
    buf[6] = throttle & 0xFF;
    buf[7] = (throttle >> 8) & 0xFF;
    buf[8] = brake & 0xFF;
    buf[9] = (brake >> 8) & 0xFF;
    buf[10] = buttons;

    uint16_t crc = crc16_ccitt(0xFFFF, &buf[2], 9);
    buf[11] = crc & 0xFF;
    buf[12] = (crc >> 8) & 0xFF;
}

/* Test-only command: valid framing and CRC, but throttle = 1001, outside the
 * STM32's permitted [0, 1000] range. */
static void make_frame_invalid(uint8_t *buf)
{
    const uint16_t invalid_throttle = 1001;
    buf[6] = invalid_throttle & 0xFF;
    buf[7] = (invalid_throttle >> 8) & 0xFF;

    uint16_t crc = crc16_ccitt(0xFFFF, &buf[2], 9);
    buf[11] = crc & 0xFF;
    buf[12] = (crc >> 8) & 0xFF;
}

// Status frame RX state machine (mirrors the STM32 side)
enum rx_state { SEEK_SYNC0, SEEK_SYNC1, COLLECT };
static enum rx_state st_state = SEEK_SYNC0;
static uint8_t st_buf[FRAME_LEN];
static uint8_t st_idx = 0;

static void handle_status_frame(const uint8_t *f)
{
    uint16_t rx_crc = f[11] | (f[12] << 8);
    uint16_t calc_crc = crc16_ccitt(0xFFFF, &f[2], 9);
    if (rx_crc != calc_crc) {
        printf("STATUS: bad CRC, dropped\n");
        return;
    }
    if (f[2] != TYPE_STATUS) {
        return;
    }

    uint8_t seq = f[3];
    uint8_t state = f[4];
    uint16_t m1 = f[5] | (f[6] << 8);
    uint16_t m2 = f[7] | (f[8] << 8);
    uint16_t srv = f[9] | (f[10] << 8);

    const char *state_str[] = {"INIT", "NORMAL", "FAILSAFE", "SELFTEST"};
    printf("STATUS seq=%u state=%s m1=%u m2=%u srv=%u\n",
          seq, (state < 4) ? state_str[state] : "UNKNOWN", m1, m2, srv);
}

static void feed_status_byte(uint8_t byte)
{
    switch (st_state) {
    case SEEK_SYNC0:
        if (byte == SYNC0) {
            st_buf[0] = byte;
            st_state = SEEK_SYNC1;
        }
        break;
    case SEEK_SYNC1:
        if (byte == SYNC1) {
            st_buf[1] = byte;
            st_idx = 2;
            st_state = COLLECT;
        } else if (byte != SYNC0) {
            st_state = SEEK_SYNC0;
        }
        break;
    case COLLECT:
        st_buf[st_idx++] = byte;
        if (st_idx >= FRAME_LEN) {
            handle_status_frame(st_buf);
            st_state = SEEK_SYNC0;
            st_idx = 0;
        }
        break;
    }
}

int main(int argc, char **argv)
{
    bool invalid_test = false;

    if (argc == 3 && strcmp(argv[2], "--invalid-test") == 0) {
        invalid_test = true;
    } else if (argc != 2) {
        fprintf(stderr, "usage: %s <serial-device> [--invalid-test]\n", argv[0]);
        return 1;
    }

    int udp_fd = open_udp();
    int serial_fd = open_serial(argv[1]);

    printf("sizeof(DIJOYSTATE2_t) = %zu, expected packet size = %zu\n",
       sizeof(DIJOYSTATE2_t), 4 + sizeof(DIJOYSTATE2_t));

    uint8_t seq = 0;
    long last_steer_raw = STEER_RAW_CENTER;
    long last_throttle_raw = THROTTLE_RAW_REST;
    long last_brake_raw = BRAKE_RAW_REST;
    uint8_t last_buttons = 0;

    struct pollfd fds[2];
    fds[0].fd = udp_fd;
    fds[0].events = POLLIN;
    fds[1].fd = serial_fd;
    fds[1].events = POLLIN;

    printf("Bridge running. Sending commands every <=%dms%s.\n", SEND_PERIOD_MS,
           invalid_test ? " (INVALID-FRAME TEST ENABLED)" : "");

    while (1) {
        int ret = poll(fds, 2, SEND_PERIOD_MS);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        // New UDP packet: update latest wheel state
        if (fds[0].revents & POLLIN) {
            uint8_t packet[4 + sizeof(DIJOYSTATE2_t)];
            ssize_t n = recvfrom(udp_fd, packet, sizeof(packet), 0, NULL, NULL);

            if (n == sizeof(packet)) {
                DIJOYSTATE2_t *js = (DIJOYSTATE2_t *)(packet + 4);
                last_steer_raw = js->lX;
                last_throttle_raw = js->lY;
                last_brake_raw = js->lRz;

                last_buttons = 0;
                if (button_pressed(js, BTN_LEFT_TURN_SIG))  last_buttons |= 0x01;
                if (button_pressed(js, BTN_RIGHT_TURN_SIG)) last_buttons |= 0x02;
                if (button_pressed(js, BTN_SELF_TEST_SIG))  last_buttons |= 0x04;
            }
        }

        // Bytes available from STM32: feed status parser
        if (fds[1].revents & POLLIN) {
            uint8_t rxbuf[64];
            ssize_t n = read(serial_fd, rxbuf, sizeof(rxbuf));
            for (ssize_t i = 0; i < n; i++) {
                feed_status_byte(rxbuf[i]);
            }
        }

        // Whether triggered by a new packet or just the 50ms timeout, send the current (possibly repeated) command state.
        int16_t steering = scale_steering(last_steer_raw);
        uint16_t throttle = scale_pedal(last_throttle_raw, THROTTLE_RAW_REST, THROTTLE_RAW_MAX);
        uint16_t brake = scale_pedal(last_brake_raw, BRAKE_RAW_REST, BRAKE_RAW_MAX);

        uint8_t frame[FRAME_LEN];
        build_command_frame(frame, steering, throttle, brake, last_buttons, seq++);
        if (invalid_test) {
            make_frame_invalid(frame);
        }

        ssize_t written = write(serial_fd, frame, FRAME_LEN);
        if (written != FRAME_LEN) {
            perror("write serial");
        }
    }

    close(udp_fd);
    close(serial_fd);
    return 0;
}
