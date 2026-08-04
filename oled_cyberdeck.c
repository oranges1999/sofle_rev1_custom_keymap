// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "oled_cyberdeck.h"

#define LINE_COLS 21

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

static void oled_write_line_final(uint8_t line, char *buf, bool invert) {
    oled_set_cursor(0, line);
    oled_write(buf, invert);
}

// Ghi đầy đủ 1 dòng 21 ký tự (không gọi oled_clear mỗi frame để giảm tải I2C).
// Tất cả dòng text đều có dấu '> ' ở đầu (kiểu terminal), trừ progress bar.
static void oled_write_line_plain(const char *text, uint8_t line, bool invert) {
    char buf[LINE_COLS + 1];
    uint8_t len = vstrlen(text);
    buf[0] = '>';
    buf[1] = ' ';
    for (uint8_t i = 0; i < LINE_COLS - 2; i++) {
        buf[i + 2] = (i < len) ? pgm_read_byte(&text[i]) : ' ';
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}

// Glitch reveal: ký tự index < reveal là thật, còn lại là ký tự rác. cursor '_' ở cuối.
static void oled_write_line_full(const char *text, uint8_t line, uint8_t reveal, bool cursor, bool invert) {
    char buf[LINE_COLS + 1];
    uint8_t len = vstrlen(text);
    buf[0] = '>';
    buf[1] = ' ';
    for (uint8_t i = 0; i < LINE_COLS - 2; i++) {
        if (i >= len) {
            buf[i + 2] = (cursor && i == len) ? '_' : ' ';
        } else {
            char c = pgm_read_byte(&text[i]);
            if (c == ' ') {
                buf[i + 2] = ' ';
            } else if (i < reveal) {
                buf[i + 2] = c;
            } else {
                buf[i + 2] = glitch_char();
            }
        }
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}

// Flicker: 1 ký tự ngẫu nhiên thành ký tự rác trong 1 frame.
static void oled_write_line_flicker(const char *text, uint8_t line, uint8_t flicker_pos, bool invert) {
    char buf[LINE_COLS + 1];
    uint8_t len = vstrlen(text);
    buf[0] = '>';
    buf[1] = ' ';
    for (uint8_t i = 0; i < LINE_COLS - 2; i++) {
        if (i >= len) {
            buf[i + 2] = ' ';
        } else {
            buf[i + 2] = (i == flicker_pos) ? glitch_char() : pgm_read_byte(&text[i]);
        }
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}

static void oled_write_layer_line(uint8_t line, const char *name, uint8_t reveal, bool invert) {
    static const char lbl[] PROGMEM = "LAYER: ";
    char buf[LINE_COLS + 1];
    uint8_t nlen = vstrlen(name);
    buf[0] = '>';
    buf[1] = ' ';
    for (uint8_t i = 0; i < LINE_COLS - 2; i++) {
        if (i < 7) {
            buf[i + 2] = pgm_read_byte(&lbl[i]);
        } else if (i < 7 + nlen) {
            char c = pgm_read_byte(&name[i - 7]);
            buf[i + 2] = (c == ' ') ? ' ' : ((i - 7) < reveal ? c : glitch_char());
        } else {
            buf[i + 2] = ' ';
        }
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}

static void oled_write_caps_line(uint8_t line, bool caps, uint8_t reveal, bool invert) {
    static const char lbl[]  PROGMEM = "CAPS ";
    static const char on_s[] PROGMEM = "ON";
    char buf[LINE_COLS + 1];
    uint8_t i = 2;
    buf[0] = '>';
    buf[1] = ' ';
    for (uint8_t k = 0; k < 5; k++) {
        buf[i++] = pgm_read_byte(&lbl[k]);
    }
    if (caps) {
        for (uint8_t k = 0; k < 2; k++) {
            char c = pgm_read_byte(&on_s[k]);
            buf[i++] = (k < reveal) ? c : glitch_char();
        }
    }
    for (; i < LINE_COLS; i++) {
        buf[i] = ' ';
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}

static void oled_write_mode_line(uint8_t line, bool mac, uint8_t reveal, bool invert) {
    static const char lbl[]   PROGMEM = "MODE ";
    static const char mac_s[] PROGMEM = "MAC";
    static const char win_s[] PROGMEM = "WIN";
    char buf[LINE_COLS + 1];
    uint8_t i = 2;
    buf[0] = '>';
    buf[1] = ' ';
    for (uint8_t k = 0; k < 5; k++) {
        buf[i++] = pgm_read_byte(&lbl[k]);
    }
    const char *m = mac ? mac_s : win_s;
    for (uint8_t k = 0; k < 3; k++) {
        char c = pgm_read_byte(&m[k]);
        buf[i++] = (k < reveal) ? c : glitch_char();
    }
    for (; i < LINE_COLS; i++) {
        buf[i] = ' ';
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}

// Dòng bar + hex feedback.
static void oled_write_status_line(uint8_t line, uint8_t fill, uint8_t bar_total, bool show_hex, uint8_t hex_val, bool fade) {
    static const char hexd[] PROGMEM = "0123456789ABCDEF";
    char buf[LINE_COLS + 1];
    uint8_t i = 0;
    for (; i < bar_total; i++) {
        buf[i] = (i < fill) ? '#' : '-';
    }
    if (show_hex) {
        buf[i++] = ' ';
        buf[i++] = '0';
        buf[i++] = 'x';
        buf[i++] = pgm_read_byte(&hexd[hex_val >> 4]);
        buf[i++] = pgm_read_byte(&hexd[hex_val & 0xF]);
    }
    for (; i < LINE_COLS; i++) {
        buf[i] = ' ';
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, fade);
}

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    if (is_keyboard_master()) {
        return OLED_ROTATION_0;
    }
    return OLED_ROTATION_180;
}

enum cyber_phase { PHASE_BOOT, PHASE_MAIN };

static enum cyber_phase phase = PHASE_BOOT;

static uint32_t boot_start = 0;

#define BOOT_TOTAL_MS  4000
#define BOOT_ONLINE_MS 3400
#define BAR_UNITS      21

static uint32_t hex_start      = 0;
static uint8_t  hex_val        = 0;

#define HEX_ANIM_MS       1000

static void render_boot(uint32_t now) {
    uint32_t t = now - boot_start;

    uint8_t rev0 = (uint8_t)(t / 100);
    if (rev0 > 9) rev0 = 9;
    oled_write_line_full(PSTR("CYBERDECK"), 0, rev0, false, false);

    uint8_t rev1 = (t > 100 ? (t - 100) / 80 : 0);
    if (rev1 > 10) rev1 = 10;
    oled_write_line_full(PSTR("INITIATING"), 1, rev1, false, false);

    uint8_t fill = (uint8_t)(t * BAR_UNITS / BOOT_TOTAL_MS);
    if (fill > BAR_UNITS) fill = BAR_UNITS;
    oled_write_status_line(2, fill, BAR_UNITS, false, 0, false);

    if (t >= BOOT_ONLINE_MS) {
        oled_write_line_full(PSTR("SYSTEM ONLINE"), 3, 13, false, true);
    } else {
        oled_write_line_plain(PSTR(""), 3, false);
    }
}

static void render_left_main(uint32_t now) {
    static const char layer_names[][8] PROGMEM = { "QWERTY", "COLEMAK", "LOWER", "RAISE", "ADJUST" };

    oled_write_line_plain(PSTR("CYBERDECK // v1.0.0"), 0, false);

    // L1: LAYER: <name>
    uint8_t layer = get_highest_layer(layer_state);
    if (layer > 4) layer = 4;
    static uint8_t  last_layer = 0xFF;
    static uint32_t layer_anim = 0;
    if (layer != last_layer) {
        last_layer = layer;
        layer_anim = now;
    }
    uint32_t lt = now - layer_anim;
    uint8_t  reveal = (lt < 300) ? (uint8_t)(lt / 40) : 21;
    oled_write_layer_line(1, layer_names[layer], reveal, false);

    // L2: CAPS indicator (glitch reveal giống dòng LAYER)
    bool caps = host_keyboard_led_state().caps_lock;
    static bool  last_caps = false;
    static uint32_t caps_anim = 0;
    if (caps != last_caps) {
        last_caps = caps;
        caps_anim = now;
    }
    uint32_t ct = now - caps_anim;
    uint8_t  creveal = (ct < 300) ? (uint8_t)(ct / 100) : 3;
    oled_write_caps_line(2, caps, creveal, false);

    // L3: MODE WIN/MAC
    bool mac = keymap_config.swap_lctl_lgui;
    static bool  last_mac = false;
    static uint32_t mode_anim = 0;
    if (mac != last_mac) {
        last_mac = mac;
        mode_anim = now;
    }
    uint8_t mreveal = ((now - mode_anim) < 300) ? (uint8_t)((now - mode_anim) / 75) : 3;
    oled_write_mode_line(3, mac, mreveal, false);
}

static const char phrases[][19] PROGMEM = {
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

    oled_write_line_plain(PSTR(""), 0, false);
    oled_write_line_plain(PSTR(""), 2, false);

    uint32_t t = now - cycle_start;
    uint8_t  len = vstrlen(phrases[phrase_idx]);

    if (t < TYPE_MS) {
        uint8_t rev = (uint8_t)(t * len / TYPE_MS);
        if (rev > len) rev = len;
        bool cur = ((now >> 8) & 1);
        oled_write_line_full(phrases[phrase_idx], 1, rev, cur, false);
    } else if (t < GLITCHOUT_T) {
        if ((prng_next() % 40) == 0) {
            oled_write_line_flicker(phrases[phrase_idx], 1, prng_next() % len, false);
        } else {
            oled_write_line_plain(phrases[phrase_idx], 1, false);
        }
    } else {
        uint8_t rev = len - (uint8_t)((t - GLITCHOUT_T) * len / (CYCLE_MS - GLITCHOUT_T));
        if (rev > len) rev = len;
        oled_write_line_full(phrases[phrase_idx], 1, rev, false, false);
        if (t >= CYCLE_MS) {
            phrase_idx = (phrase_idx + 1) % NUM_PHRASES;
            cycle_start = now;
        }
    }

    // L3: progress bar + hex feedback
    uint8_t fill = (uint8_t)(t * BAR_UNITS / CYCLE_MS);
    if (fill > BAR_UNITS) fill = BAR_UNITS;
    uint32_t ht = now - hex_start;
    if (hex_start != 0 && ht < HEX_ANIM_MS) {
        bool fade = (ht > HEX_ANIM_MS * 3 / 4) ? ((now >> 8) & 1) : false;
        if (ht > HEX_ANIM_MS / 2 && (prng_next() & 3) == 0) {
            oled_write_status_line(3, fill, 14, true, (uint8_t)(prng_next() & 0xFF), fade);
        } else {
            oled_write_status_line(3, fill, 14, true, hex_val, fade);
        }
        if (ht >= HEX_ANIM_MS) hex_start = 0;
    } else {
        oled_write_status_line(3, fill, BAR_UNITS, false, 0, false);
    }
}

// ---- Full-screen glitch + tearing (định kỳ) ----
static uint32_t next_glitch = 0;
static uint32_t glitch_end  = 0;

#define GLITCH_PERIOD_MS 2500
#define GLITCH_BURST_MS  300

static void oled_apply_glitch(void) {
    uint32_t now = timer_read32();
    if (now >= next_glitch) {
        glitch_end = now + GLITCH_BURST_MS;
        next_glitch = now + GLITCH_PERIOD_MS;
    }
    if (now >= glitch_end) return;

    uint8_t shift[4];
    for (uint8_t p = 0; p < 4; p++) {
        shift[p] = prng_next() % 7; // 0..6 px
    }

    bool jump      = ((prng_next() & 7) == 0); // ~1/8 frame: nhân đôi band
    int8_t jump_off = (prng_next() & 1) ? 1 : -1; // +1 hoặc -1 band

    for (uint8_t p = 0; p < 4; p++) {
        uint8_t band[128];
        oled_buffer_reader_t r = oled_read_raw(p * 128);

        uint8_t n    = shift[p];
        bool    left = (n > 0) && (prng_next() & 1);
        for (uint8_t i = 0; i < 128; i++) {
            if (left) {
                band[i] = (i + n < 128) ? r.current_element[i + n] : 0;
            } else {
                band[i] = (i >= n) ? r.current_element[i - n] : 0;
            }
        }

        if (jump) {
            uint8_t src = (p + jump_off + 4) % 4;
            oled_buffer_reader_t s = oled_read_raw(src * 128);
            uint8_t sn = shift[src];
            for (uint8_t i = 0; i < 128; i++) {
                band[i] = (i >= sn) ? s.current_element[i - sn] : 0;
            }
        }

        if ((prng_next() & 3) == 0) { // ~1/4 frame: noise bar
            uint8_t start = prng_next() % 100;
            uint8_t width = 2 + (prng_next() % 8);
            for (uint8_t i = 0; i < width && (start + i) < 128; i++) {
                band[start + i] = prng_next();
            }
        }

        oled_set_cursor(0, p);
        oled_write_raw((const char *)band, 128);
    }
}

static void oled_task_user_impl(void) {
    uint32_t now = timer_read32();

    if (phase == PHASE_BOOT) {
        if (boot_start == 0) boot_start = now;
        if (now - boot_start >= BOOT_TOTAL_MS) {
            phase = PHASE_MAIN;
            oled_clear();
        } else {
            render_boot(now);
        }
        return;
    }

    oled_clear();
    if (is_keyboard_master()) {
        render_left_main(now);
    } else {
        render_right_main(now);
    }
    oled_apply_glitch();
}

bool oled_task_user(void) {
    oled_task_user_impl();
    return false; // ngăn board-level oled_task_kb (sofle.c) vẽ text/QMK logo mặc định
}

void cyberdeck_key_pressed(void) {
    if (phase != PHASE_MAIN) return;

    uint32_t now = timer_read32();
    hex_start = now;
    hex_val = prng_next() & 0xFF;
}
