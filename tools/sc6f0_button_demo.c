#include "sc6f0_button.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

static volatile bool g_running = true;

static void sigint_handler(int sig) {
    (void)sig;
    g_running = false;
}

int main(int argc, char **argv) {
    const char *dev = (argc > 1) ? argv[1] : NULL;

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    printf("Opening SC6F0 button device...\n");
    int fd = sc6f0_button_open(dev);
    if (fd < 0) {
        fprintf(stderr, "Failed to open button device: %d\n", fd);
        return 1;
    }

    uint8_t states = 0;
    if (sc6f0_button_get_states(fd, &states) == 0) {
        printf("Current instantaneous states:\n");
        for (int i = 0; i < SC6F0_BTN_COUNT; i++) {
            bool pressed = (states & (1 << i)) != 0;
            printf("  %s (BTN_%d): %s\n", sc6f0_button_name((sc6f0_button_id_t)i), i,
                   pressed ? "PRESSED" : "RELEASED");
        }
    }

    printf("\nStarting event monitor loop (press Ctrl+C to exit)...\n");
    uint64_t press_times[SC6F0_BTN_COUNT] = {0};

    while (g_running) {
        sc6f0_button_event_t ev;
        int ret = sc6f0_button_read_event(fd, &ev, 500); // 500ms timeout
        if (ret < 0) {
            fprintf(stderr, "Read error: %d\n", ret);
            break;
        }
        if (ret == 0) {
            continue; // timeout
        }

        time_t sec = ev.timestamp_us / 1000000ULL;
        uint32_t ms = (ev.timestamp_us % 1000000ULL) / 1000;
        struct tm tm_buf;
        localtime_r(&sec, &tm_buf);

        if (ev.action == SC6F0_BTN_ACTION_PRESS) {
            press_times[ev.id] = ev.timestamp_us;
            printf("[%02d:%02d:%02d.%03u] [PRESS]   %s (BTN_%d, 0x%03x)\n",
                   tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms,
                   ev.name, ev.id, ev.linux_code);
        } else if (ev.action == SC6F0_BTN_ACTION_RELEASE) {
            uint64_t held_us = 0;
            if (press_times[ev.id] > 0) {
                held_us = ev.timestamp_us - press_times[ev.id];
                press_times[ev.id] = 0;
            }
            printf("[%02d:%02d:%02d.%03u] [RELEASE] %s (BTN_%d, 0x%03x) held %.1f ms\n",
                   tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ms,
                   ev.name, ev.id, ev.linux_code, held_us / 1000.0);
        }
        fflush(stdout);
    }

    printf("\nExiting button monitor.\n");
    sc6f0_button_close(fd);
    return 0;
}
