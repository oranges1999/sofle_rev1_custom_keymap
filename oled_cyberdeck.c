// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "oled_cyberdeck.h"

#define V_COLS 5

static uint32_t prng_state = 0xC0FFEE42;

static uint32_t prng_next(void) {
    prng_state ^= prng_state << 13;
    prng_state ^= prng_state >> 17;
    prng_state ^= prng_state << 5;
    return prng_state;
}

static char glitch_char(void) {
    static const char charset[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+#@!";
    return pgm_read_byte(&charset[prng_next() % (sizeof(charset) - 1)]);
}

static uint8_t vstrlen(const char *text) {
    uint8_t n = 0;
    while (pgm_read_byte(&text[n]) != '\0') n++;
    return n;
}

static uint8_t oled_write_vertical(const char *text, uint8_t start, bool invert) {
    uint8_t len = vstrlen(text);
    uint8_t line = start;
    for (uint8_t i = 0; i < len; i += V_COLS) {
        char buf[V_COLS + 1];
        for (uint8_t k = 0; k < V_COLS; k++) {
            buf[k] = (i + k < len) ? pgm_read_byte(&text[i + k]) : ' ';
        }
        buf[V_COLS] = '\0';
        oled_set_cursor(0, line++);
        oled_write(buf, invert);
    }
    return line;
}

static uint8_t oled_write_vertical_glitched(const char *text, uint8_t start, uint8_t reveal, bool invert) {
    uint8_t len = vstrlen(text);
    uint8_t line = start;
    for (uint8_t i = 0; i < len; i += V_COLS) {
        char buf[V_COLS + 1];
        for (uint8_t k = 0; k < V_COLS; k++) {
            uint8_t idx = i + k;
            if (idx >= len) {
                buf[k] = ' ';
            } else {
                char c = pgm_read_byte(&text[idx]);
                if (c == ' ') {
                    buf[k] = ' ';
                } else if (idx < reveal) {
                    buf[k] = c;
                } else {
                    buf[k] = glitch_char();
                }
            }
        }
        buf[V_COLS] = '\0';
        oled_set_cursor(0, line++);
        oled_write(buf, invert);
    }
    return line;
}

static uint8_t oled_write_vertical_flicker(const char *text, uint8_t start, uint8_t flicker_pos, bool invert) {
    uint8_t len = vstrlen(text);
    uint8_t line = start;
    for (uint8_t i = 0; i < len; i += V_COLS) {
        char buf[V_COLS + 1];
        for (uint8_t k = 0; k < V_COLS; k++) {
            uint8_t idx = i + k;
            if (idx >= len) {
                buf[k] = ' ';
            } else {
                buf[k] = (idx == flicker_pos) ? glitch_char() : pgm_read_byte(&text[idx]);
            }
        }
        buf[V_COLS] = '\0';
        oled_set_cursor(0, line++);
        oled_write(buf, invert);
    }
    return line;
}

static void oled_write_vertical_bar(uint8_t start, uint8_t filled) {
    char buf[V_COLS + 1];
    for (uint8_t l = 0; l < 3; l++) {
        for (uint8_t c = 0; c < V_COLS; c++) {
            buf[c] = ((uint8_t)(l * V_COLS + c) < filled) ? '#' : '-';
        }
        buf[V_COLS] = '\0';
        oled_set_cursor(0, start + l);
        oled_write(buf, false);
    }
}

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    if (is_keyboard_master()) {
        return OLED_ROTATION_270;
    }
    return OLED_ROTATION_90;
}

enum cyber_phase { PHASE_BOOT, PHASE_MAIN };
enum main_state  { MAIN_NORMAL, MAIN_SHUTDOWN, MAIN_SCREEN_OFF, MAIN_WAKE };

static enum cyber_phase phase = PHASE_BOOT;
static enum main_state  mstate = MAIN_NORMAL;

static uint32_t boot_start = 0;

#define BOOT_TOTAL_MS  4000
#define BOOT_ONLINE_MS 3400
#define BAR_UNITS      15

static uint32_t last_input     = 0;
static uint32_t shutdown_start = 0;
static uint32_t wake_start     = 0;
static uint32_t hex_start      = 0;
static uint8_t  hex_val        = 0;

#define IDLE_TIMEOUT_MS   120000
#define SHUTDOWN_ANIM_MS  500
#define WAKE_ANIM_MS      500
#define HEX_ANIM_MS       1000

static void render_boot(uint32_t now) {
    uint32_t t = now - boot_start;

    uint8_t rev0 = (uint8_t)(t / 100);
    if (rev0 > 9) rev0 = 9;
    oled_write_vertical_glitched(PSTR("CYBERDECK"), 0, rev0, false);

    uint8_t rev1 = (t > 100 ? (t - 100) / 80 : 0);
    if (rev1 > 10) rev1 = 10;
    oled_write_vertical_glitched(PSTR("INITIATING"), 2, rev1, false);

    uint8_t fill = (uint8_t)(t * BAR_UNITS / BOOT_TOTAL_MS);
    if (fill > BAR_UNITS) fill = BAR_UNITS;
    oled_write_vertical_bar(4, fill);

    if (t >= BOOT_ONLINE_MS) {
        oled_write_vertical_glitched(PSTR("SYSTEM ONLINE"), 8, 13, true);
    }
}

static void oled_write_glitched_line(const char *text, uint8_t reveal, uint8_t len, bool invert) {
    char buf[V_COLS + 1];
    for (uint8_t i = 0; i < len; i++) {
        char c = pgm_read_byte(&text[i]);
        if (c == ' ') {
            buf[i] = ' ';
        } else if (i < reveal) {
            buf[i] = c;
        } else {
            buf[i] = glitch_char();
        }
    }
    buf[len] = '\0';
    oled_write(buf, invert);
}

static void oled_write_hex(uint8_t val, bool invert) {
    static const char hexdigits[] PROGMEM = "0123456789ABCDEF";
    char buf[5];
    buf[0] = '0';
    buf[1] = 'x';
    buf[2] = pgm_read_byte(&hexdigits[val >> 4]);
    buf[3] = pgm_read_byte(&hexdigits[val & 0xF]);
    buf[4] = '\0';
    oled_write(buf, invert);
}

static void render_left_main(uint32_t now) {
    oled_write_vertical(PSTR("CYBERDECK"), 0, false);

    static const char layer_names[][8] PROGMEM = { "QWERTY", "COLEMAK", "LOWER", "RAISE", "ADJUST" };

    // L3 nhãn LAYER, L4-5 tên layer
    oled_write_vertical(PSTR("LAYER"), 3, false);
    uint8_t layer = get_highest_layer(layer_state);
    if (layer > 4) layer = 4;
    static uint8_t  last_layer = 0xFF;
    static uint32_t layer_anim = 0;
    if (layer != last_layer) {
        last_layer = layer;
        layer_anim = now;
    }
    uint32_t lt = now - layer_anim;
    if (lt < 300) {
        oled_write_vertical_glitched(layer_names[layer], 4, (uint8_t)(lt / 40), false);
    } else {
        oled_write_vertical(layer_names[layer], 4, false);
    }

    // L6 nhãn CAPS, L7 trạng thái
    oled_write_vertical(PSTR("CAPS"), 6, false);
    bool caps = host_keyboard_led_state().caps_lock;
    static bool  last_caps = false;
    static uint32_t caps_anim = 0;
    if (caps != last_caps) {
        last_caps = caps;
        caps_anim = now;
    }
    oled_set_cursor(0, 7);
    if (caps) {
        bool blink = (now - caps_anim) < 400 && ((now >> 8) & 1);
        oled_write_P(PSTR("ON"), blink);
    } else {
        oled_write_P(PSTR("   "), false);
    }

    // L8 nhãn MODE, L9 WIN/MAC
    oled_write_vertical(PSTR("MODE"), 8, false);
    bool mac = keymap_config.swap_lctl_lgui;
    static bool  last_mac = false;
    static uint32_t mode_anim = 0;
    if (mac != last_mac) {
        last_mac = mac;
        mode_anim = now;
    }
    oled_set_cursor(0, 9);
    if ((now - mode_anim) < 300) {
        oled_write_glitched_line(PSTR("WIN"), (uint8_t)((now - mode_anim) / 75), 3, true);
    } else {
        oled_write_P(mac ? PSTR("MAC") : PSTR("WIN"), false);
    }
}

static const char phrases[][18] PROGMEM = {
    "BRAINDANCE LOAD...", "QUICKHACK", "NETRUNNER", "JACK IN", "ICEBREAK",
    "DAEMON UPLOAD", "RELIC DETECTED", "TRACE PROTOCOL", "FLATLINE", "SHARD COMPLETE"
};
#define NUM_PHRASES (sizeof(phrases) / sizeof(phrases[0]))

#define CYCLE_MS     3000
#define TYPE_MS      1000
#define GLITCHOUT_T  (CYCLE_MS - 250)

static void render_right_main(uint32_t now) {
    static uint8_t  phrase_idx = 0;
    static uint32_t cycle_start = 0;

    uint32_t t = now - cycle_start;
    uint8_t  len = vstrlen(phrases[phrase_idx]);

    // Căn giữa câu trong vùng L1-8
    uint8_t phrase_lines = (len + V_COLS - 1) / V_COLS;
    uint8_t area_start = (uint8_t)(4 - phrase_lines / 2 + 1);
    if (area_start < 1) area_start = 1;

    if (t < TYPE_MS) {
        uint8_t rev = (uint8_t)(t * len / TYPE_MS);
        if (rev > len) rev = len;
        oled_write_vertical_glitched(phrases[phrase_idx], area_start, rev, false);
    } else if (t < GLITCHOUT_T) {
        if ((prng_next() % 40) == 0) {
            oled_write_vertical_flicker(phrases[phrase_idx], area_start, prng_next() % len, false);
        } else {
            oled_write_vertical(phrases[phrase_idx], area_start, false);
        }
    } else {
        uint8_t rev = len - (uint8_t)((t - GLITCHOUT_T) * len / (CYCLE_MS - GLITCHOUT_T));
        if (rev > len) rev = len;
        oled_write_vertical_glitched(phrases[phrase_idx], area_start, rev, false);
        if (t >= CYCLE_MS) {
            phrase_idx = (phrase_idx + 1) % NUM_PHRASES;
            cycle_start = now;
        }
    }

    // Progress bar dọc L12-14
    uint8_t fill = (uint8_t)(t * BAR_UNITS / CYCLE_MS);
    if (fill > BAR_UNITS) fill = BAR_UNITS;
    oled_write_vertical_bar(12, fill);

    // Hex feedback L15
    uint32_t ht = now - hex_start;
    if (hex_start != 0 && ht < HEX_ANIM_MS) {
        oled_set_cursor(0, 15);
        if (ht > HEX_ANIM_MS / 2 && (prng_next() & 3) == 0) {
            oled_write_hex(prng_next() & 0xFF, false); // giá trị nhiễu
        } else {
            oled_write_hex(hex_val, (ht > HEX_ANIM_MS * 3 / 4) ? ((now >> 8) & 1) : false);
        }
        if (ht >= HEX_ANIM_MS) hex_start = 0;
    }
}

static void oled_task_user_impl(void) {
    uint32_t now = timer_read32();

    if (phase == PHASE_BOOT) {
        if (boot_start == 0) boot_start = now;
        if (now - boot_start >= BOOT_TOTAL_MS) {
            phase = PHASE_MAIN;
            last_input = now;
            oled_clear();
        } else {
            render_boot(now);
        }
        return;
    }

    switch (mstate) {
        case MAIN_NORMAL:
            if (is_keyboard_master()) {
                render_left_main(now);
            } else {
                render_right_main(now);
            }
            if (now - last_input >= IDLE_TIMEOUT_MS) {
                mstate = MAIN_SHUTDOWN;
                shutdown_start = now;
            }
            break;
        case MAIN_SHUTDOWN:
            oled_write_vertical_glitched(PSTR("SYSTEM OFFLINE"), 5, 0, true);
            if (now - shutdown_start >= SHUTDOWN_ANIM_MS) {
                oled_off();
                mstate = MAIN_SCREEN_OFF;
            }
            break;
        case MAIN_SCREEN_OFF:
            break;
        case MAIN_WAKE:
            oled_write_vertical_glitched(PSTR("REBOOTING"), 6, (uint8_t)((now - wake_start) / 60), true);
            if (now - wake_start >= WAKE_ANIM_MS) {
                oled_clear();
                mstate = MAIN_NORMAL;
            }
            break;
    }
}

bool oled_task_user(void) {
    oled_clear();
    oled_task_user_impl();
    return true;
}

void cyberdeck_key_pressed(void) {
    uint32_t now = timer_read32();
    last_input = now;
    if (phase != PHASE_MAIN) return;

    if (mstate == MAIN_SCREEN_OFF || mstate == MAIN_SHUTDOWN) {
        oled_on();
        mstate = MAIN_WAKE;
        wake_start = now;
    } else if (mstate == MAIN_NORMAL) {
        hex_start = now;
        hex_val = prng_next() & 0xFF;
    }
}
