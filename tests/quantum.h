// Stub API QMK, chỉ đủ để biên dịch oled_cyberdeck.c trên máy phát triển.
// File này KHÔNG tham gia build firmware — nó chỉ nằm trên include path của test.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PROGMEM
#define PSTR(x) (x)
#define pgm_read_byte(p) ((uint8_t) * (const char *)(p))

typedef uint8_t oled_rotation_t;
#define OLED_ROTATION_0 0
#define OLED_ROTATION_180 2

typedef struct {
    const uint8_t *current_element;
    uint16_t       remaining_element_count;
} oled_buffer_reader_t;

typedef uint32_t layer_state_t;

typedef struct {
    bool caps_lock;
} led_t;

typedef struct {
    bool swap_lctl_lgui;
} keymap_config_t;

// Trạng thái giả lập. Test đọc stub_screen để kiểm nội dung đã ghi ra màn.
static char     stub_screen[4][22];
static bool     stub_invert[4];
static uint8_t  stub_cursor_line;
static uint32_t stub_timer;
static uint32_t stub_activity;
static bool     stub_master;
static uint8_t  stub_raw[512];

static layer_state_t   layer_state;
static keymap_config_t keymap_config;
static led_t           stub_led;

static inline void oled_set_cursor(uint8_t col, uint8_t line) {
    (void)col;
    stub_cursor_line = line;
}

static inline void oled_write(const char *data, bool invert) {
    // 21 ở đây khớp với LINE_COLS trong oled_cyberdeck.c — nếu LINE_COLS đổi,
    // sửa luôn con số này (và kích thước stub_screen ở trên).
    memcpy(stub_screen[stub_cursor_line], data, 21);
    stub_screen[stub_cursor_line][21] = '\0';
    stub_invert[stub_cursor_line]     = invert;
}

static inline void oled_clear(void) {
    memset(stub_screen, ' ', sizeof(stub_screen));
}

static inline oled_buffer_reader_t oled_read_raw(uint16_t start_index) {
    oled_buffer_reader_t r = {&stub_raw[start_index], (uint16_t)(sizeof(stub_raw) - start_index)};
    return r;
}

static inline void oled_write_raw(const char *data, uint16_t size) {
    (void)data;
    (void)size;
}

static inline uint32_t timer_read32(void) {
    return stub_timer;
}

static inline uint32_t last_matrix_activity_time(void) {
    return stub_activity;
}

static inline bool is_keyboard_master(void) {
    return stub_master;
}

static inline uint8_t get_highest_layer(layer_state_t s) {
    return (uint8_t)s;
}

static inline led_t host_keyboard_led_state(void) {
    return stub_led;
}
