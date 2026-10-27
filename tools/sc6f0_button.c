#define _GNU_SOURCE
#include "sc6f0_button.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static const char *k_button_names[SC6F0_BTN_COUNT] = {
    "sw0", "sw1", "sw2", "sw3", "sw4"
};

const char *sc6f0_button_name(sc6f0_button_id_t id) {
    if (id >= 0 && id < SC6F0_BTN_COUNT) {
        return k_button_names[id];
    }
    return "unknown";
}

static sc6f0_button_id_t code_to_id(uint16_t code) {
    switch (code) {
        case SC6F0_KEY_CODE_BTN_0: return SC6F0_BTN_SW0;
        case SC6F0_KEY_CODE_BTN_1: return SC6F0_BTN_SW1;
        case SC6F0_KEY_CODE_BTN_2: return SC6F0_BTN_SW2;
        case SC6F0_KEY_CODE_BTN_3: return SC6F0_BTN_SW3;
        case SC6F0_KEY_CODE_BTN_4: return SC6F0_BTN_SW4;
        default: return SC6F0_BTN_UNKNOWN;
    }
}

static int auto_detect_device(char *out_path, size_t max_len) {
    DIR *d = opendir("/sys/class/input");
    if (!d) return -ENOENT;

    struct dirent *de;
    char path[512];
    char name_buf[128];

    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;

        snprintf(path, sizeof(path), "/sys/class/input/%.200s/device/name", de->d_name);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            ssize_t n = read(fd, name_buf, sizeof(name_buf) - 1);
            close(fd);
            if (n > 0) {
                name_buf[n] = '\0';
                if (strstr(name_buf, "gpio-keys") != NULL) {
                    snprintf(out_path, max_len, "/dev/input/%.100s", de->d_name);
                    closedir(d);
                    return 0;
                }
            }
        }
    }
    closedir(d);

    if (access("/dev/input/event0", R_OK) == 0) {
        snprintf(out_path, max_len, "/dev/input/event0");
        return 0;
    }
    return -ENOENT;
}

int sc6f0_button_open(const char *dev_path) {
    char detected_path[128];
    const char *target = dev_path;

    if (!target) {
        int ret = auto_detect_device(detected_path, sizeof(detected_path));
        if (ret < 0) return ret;
        target = detected_path;
    }

    int fd = open(target, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    return fd;
}

void sc6f0_button_close(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

int sc6f0_button_get_states(int fd, uint8_t *states_mask) {
    if (fd < 0 || !states_mask) return -EINVAL;

    uint8_t key_bits[(KEY_MAX + 7) / 8];
    memset(key_bits, 0, sizeof(key_bits));

    if (ioctl(fd, EVIOCGKEY(sizeof(key_bits)), key_bits) < 0) {
        return -errno;
    }

    uint8_t mask = 0;
    for (int i = 0; i < SC6F0_BTN_COUNT; i++) {
        uint16_t code = SC6F0_KEY_CODE_BTN_0 + i;
        int byte_idx = code / 8;
        int bit_idx = code % 8;
        if (key_bits[byte_idx] & (1 << bit_idx)) {
            mask |= (1 << i);
        }
    }
    *states_mask = mask;
    return 0;
}

int sc6f0_button_read_event(int fd, sc6f0_button_event_t *event, int timeout_ms) {
    if (fd < 0 || !event) return -EINVAL;

    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) return -errno;
    if (ret == 0) return 0; // timeout

    struct input_event ev;
    while (1) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -errno;
        }
        if (n != sizeof(ev)) {
            return -EIO;
        }

        if (ev.type == EV_KEY) {
            sc6f0_button_id_t id = code_to_id(ev.code);
            if (id != SC6F0_BTN_UNKNOWN) {
                event->id = id;
                event->name = sc6f0_button_name(id);
                event->linux_code = ev.code;
                event->action = (sc6f0_button_action_t)ev.value;
                event->timestamp_us = (uint64_t)ev.time.tv_sec * 1000000ULL + ev.time.tv_usec;
                return 1;
            }
        }
    }
}
