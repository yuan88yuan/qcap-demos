#ifndef SC6F0_BUTTON_H
#define SC6F0_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * SC6F0 Button Identifiers and Linux Key Codes
 */
typedef enum {
    SC6F0_BTN_SW0 = 0,
    SC6F0_BTN_SW1 = 1,
    SC6F0_BTN_SW2 = 2,
    SC6F0_BTN_SW3 = 3,
    SC6F0_BTN_SW4 = 4,
    SC6F0_BTN_COUNT = 5,
    SC6F0_BTN_UNKNOWN = -1
} sc6f0_button_id_t;

#define SC6F0_KEY_CODE_BTN_0 0x100
#define SC6F0_KEY_CODE_BTN_1 0x101
#define SC6F0_KEY_CODE_BTN_2 0x102
#define SC6F0_KEY_CODE_BTN_3 0x103
#define SC6F0_KEY_CODE_BTN_4 0x104

typedef enum {
    SC6F0_BTN_ACTION_RELEASE = 0,
    SC6F0_BTN_ACTION_PRESS   = 1,
    SC6F0_BTN_ACTION_REPEAT  = 2
} sc6f0_button_action_t;

typedef struct {
    sc6f0_button_id_t id;
    const char *name;          /* "sw0", "sw1", etc. */
    uint16_t linux_code;       /* 0x100 ~ 0x104 */
    sc6f0_button_action_t action;
    uint64_t timestamp_us;     /* Microseconds */
} sc6f0_button_event_t;

/**
 * Open the SC6F0 gpio-keys device node.
 * Automatically searches /dev/input/event* for "gpio-keys" if dev_path is NULL.
 * Returns file descriptor >= 0 on success, or negative errno on error.
 */
int sc6f0_button_open(const char *dev_path);

/**
 * Close the button device.
 */
void sc6f0_button_close(int fd);

/**
 * Query current instantaneous state of all 5 buttons via EVIOCGKEY ioctl.
 * @param fd open button device fd
 * @param states_mask pointer to uint8_t bitmask (bit 0 = SW0, bit 1 = SW1, ..., 1=pressed, 0=released)
 * Returns 0 on success, negative errno on failure.
 */
int sc6f0_button_get_states(int fd, uint8_t *states_mask);

/**
 * Read the next button event.
 * @param fd open button device fd
 * @param event pointer to output event struct
 * @param timeout_ms Timeout in milliseconds (<0 for blocking, 0 for immediate nonblocking, >0 timeout)
 * Returns 1 if event read, 0 on timeout, negative errno on failure.
 */
int sc6f0_button_read_event(int fd, sc6f0_button_event_t *event, int timeout_ms);

/**
 * Helper to convert button ID to string.
 */
const char *sc6f0_button_name(sc6f0_button_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* SC6F0_BUTTON_H */
