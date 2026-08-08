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
// 21 ở đây khớp với LINE_COLS trong oled_cyberdeck.c — nếu LINE_COLS đổi,
// sửa luôn mọi con số 21 trong file này.
static char     stub_screen[4][22];
static bool     stub_invert[4];    // invert của lần oled_write() cuối trên dòng
static bool     stub_inv_cell[4][21]; // invert theo từng ô, để test con trỏ khối
static uint8_t  stub_cursor_line;
static uint8_t  stub_cursor_col;
static uint32_t stub_timer;
static uint32_t stub_activity;
static bool     stub_master;
static uint8_t  stub_raw[512];

static layer_state_t   layer_state;
static keymap_config_t keymap_config;
static led_t           stub_led;

static inline void oled_set_cursor(uint8_t col, uint8_t line) {
    stub_cursor_col  = col;
    stub_cursor_line = line;
}

// Ghi từ vị trí con trỏ hiện tại rồi đẩy con trỏ đi, như driver thật.
static inline void oled_write(const char *data, bool invert) {
    uint8_t col = stub_cursor_col;
    for (; col < 21 && *data; col++, data++) {
        stub_screen[stub_cursor_line][col]   = *data;
        stub_inv_cell[stub_cursor_line][col] = invert;
    }
    stub_screen[stub_cursor_line][21] = '\0';
    stub_invert[stub_cursor_line]     = invert;
    stub_cursor_col                   = col;
}

static inline void oled_write_char(const char data, bool invert) {
    if (stub_cursor_col < 21) {
        stub_screen[stub_cursor_line][stub_cursor_col]   = data;
        stub_inv_cell[stub_cursor_line][stub_cursor_col] = invert;
        stub_cursor_col++;
    }
}

static inline void oled_clear(void) {
    memset(stub_screen, ' ', sizeof(stub_screen));
    memset(stub_inv_cell, 0, sizeof(stub_inv_cell));
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
