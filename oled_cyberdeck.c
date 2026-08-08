// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"

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

// Quy tắc decode dùng chung: idx < reveal là ký tự thật, idx == reveal là ký
// tự rác đang giải mã (đóng vai trò con trỏ), còn lại (kể cả vượt quá len) để trống.
static char decode_char(const char *text, uint8_t idx, uint8_t len, uint8_t reveal) {
    if (idx >= len) return ' ';
    if (idx < reveal) return pgm_read_byte(&text[idx]);
    if (idx == reveal) return glitch_char();
    return ' ';
}

// Decode từng ký tự: xem decode_char(). Ký tự rác ở vị trí reveal đóng vai trò con trỏ.
static void oled_write_line_type(const char *text, uint8_t line, uint8_t reveal, bool invert) {
    char    buf[LINE_COLS + 1];
    uint8_t len = vstrlen(text);
    buf[0] = '>';
    buf[1] = ' ';
    for (uint8_t i = 0; i < LINE_COLS - 2; i++) {
        buf[i + 2] = decode_char(text, i, len, reveal);
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}

// Info line: "LABEL:" pad tới INFO_LABEL_W, rồi value (glitch reveal). Các dòng LAYER/CAPSLOCK/MODE đồng đều.
#define INFO_LABEL_W 10

static void oled_write_info_line(uint8_t line, const char *label, const char *value, uint8_t reveal, bool invert) {
    char buf[LINE_COLS + 1];
    uint8_t i = 0;
    for (; i < INFO_LABEL_W; i++) {
        char c = pgm_read_byte(&label[i]);
        if (c == '\0') break;
        buf[i] = c;
    }
    for (; i < INFO_LABEL_W; i++) {
        buf[i] = ' ';
    }
    uint8_t vlen = vstrlen(value);
    for (; i < LINE_COLS; i++) {
        uint8_t k = i - INFO_LABEL_W;
        buf[i]    = decode_char(value, k, vlen, reveal);
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}

// Thanh progress 21 ký tự, không có tiền tố "> ".
static void oled_write_bar_line(uint8_t line, uint8_t fill) {
    char buf[LINE_COLS + 1];
    for (uint8_t i = 0; i < LINE_COLS; i++) {
        buf[i] = (i < fill) ? '#' : '-';
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, false);
}

// Dòng báo nạp xong. "[" nằm ở cột 3, không có tiền tố "> ".
static void oled_write_loaded_line(uint8_t line) {
    static const char text[] PROGMEM = "   [ LOADED ]";
    char              buf[LINE_COLS + 1];
    uint8_t           len = vstrlen(text);
    for (uint8_t i = 0; i < LINE_COLS; i++) {
        buf[i] = (i < len) ? pgm_read_byte(&text[i]) : ' ';
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, false);
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
static uint32_t main_start = 0;

#define BOOT_T1_MS    900  // xong "CYBERDECK"
#define BOOT_T2_MS   2000  // xong "INITIATING"
#define BOOT_T3_MS   3500  // thanh progress đầy
#define BOOT_T4_MS   3600  // hết khoảng xoá màn
#define BOOT_TOTAL_MS 5000 // hết nhấp nháy SYSTEM ONLINE, vào màn chính
#define BOOT_BLINK_MS 350  // nửa chu kỳ nhấp nháy SYSTEM ONLINE

static void render_boot(uint32_t now) {
    uint32_t t = now - boot_start;

    if (t < BOOT_T3_MS) {
        // Giai đoạn gõ chữ rồi chạy thanh progress.
        oled_write_line_type(PSTR("CYBERDECK"), 0, (uint8_t)(t / 100), false);

        if (t >= BOOT_T1_MS) {
            oled_write_line_type(PSTR("INITIATING"), 1, (uint8_t)((t - BOOT_T1_MS) / 110), false);
        } else {
            oled_write_line_plain(PSTR(""), 1, false);
        }

        if (t >= BOOT_T2_MS) {
            uint32_t bt = t - BOOT_T2_MS;
            // Chia làm tròn lên: bt chỉ tới 1499 nên chia thường sẽ chốt ở 20,
            // thanh bar không bao giờ đầy trước lúc xoá màn.
            uint16_t span = BOOT_T3_MS - BOOT_T2_MS;
            uint8_t  fill = (uint8_t)((bt * LINE_COLS + span - 1) / span);
            oled_write_bar_line(2, fill);
        } else {
            oled_write_line_plain(PSTR(""), 2, false);
        }

        oled_write_line_plain(PSTR(""), 3, false);
        return;
    }

    // Xoá màn rồi nhấp nháy SYSTEM ONLINE bằng cách đảo invert.
    oled_write_line_plain(PSTR(""), 0, false);
    oled_write_line_plain(PSTR(""), 2, false);
    oled_write_line_plain(PSTR(""), 3, false);

    if (t < BOOT_T4_MS) {
        oled_write_line_plain(PSTR(""), 1, false);
    } else {
        bool inv = (((t - BOOT_T4_MS) / BOOT_BLINK_MS) & 1) == 0;
        oled_write_line_type(PSTR("SYSTEM ONLINE"), 1, 13, inv);
    }
}

#define MAIN_L0_MS  400  // xong dòng 0 (header)
#define MAIN_L1_MS  650  // xong dòng 1 (LAYER)
#define MAIN_L2_MS  900  // xong dòng 2 (CAPSLOCK)
#define MAIN_L3_MS 1150  // xong dòng 3 (MODE)

#define REVEAL_HIDDEN 0xFF
#define REVEAL_DONE   LINE_COLS

// Reveal của một dòng info. Trong cửa sổ load đầu tiên thì chạy theo tm; qua
// cửa sổ đó thì chạy theo animation khi giá trị đổi; ngoài ra hiện đầy.
// Trả REVEAL_HIDDEN nghĩa là dòng chưa tới lượt, phải để trống.
static uint8_t info_reveal(uint32_t tm, uint16_t from, uint16_t to, uint32_t since_change) {
    if (tm < from) return REVEAL_HIDDEN;
    if (tm < to) return (uint8_t)((tm - from) * 8 / (to - from));
    if (since_change < 300) return (uint8_t)(since_change / 40);
    return REVEAL_DONE;
}

static void render_left_main(uint32_t now) {
    static const char layer_names[][8] PROGMEM = {"QWERTY", "COLEMAK", "LOWER", "RAISE", "ADJUST"};
    static const char header[] PROGMEM         = "CYBERDECK // v1.0.1";

    uint32_t tm = now - main_start;

    // Dòng 0: header, decode trong cửa sổ load đầu tiên.
    if (tm < MAIN_L0_MS) {
        oled_write_line_type(header, 0, (uint8_t)(tm * 19 / MAIN_L0_MS), false);
    } else {
        oled_write_line_plain(header, 0, false);
    }

    uint8_t layer = get_highest_layer(layer_state);
    if (layer > 4) layer = 4;
    static uint8_t  last_layer = 0xFF;
    static uint32_t layer_anim = 0;
    if (layer != last_layer) {
        last_layer = layer;
        layer_anim = now;
    }

    bool            caps      = host_keyboard_led_state().caps_lock;
    static bool     last_caps = false;
    static uint32_t caps_anim = 0;
    if (caps != last_caps) {
        last_caps = caps;
        caps_anim = now;
    }

    bool            mac       = keymap_config.swap_lctl_lgui;
    static bool     last_mac  = false;
    static uint32_t mode_anim = 0;
    if (mac != last_mac) {
        last_mac  = mac;
        mode_anim = now;
    }

    uint8_t r1 = info_reveal(tm, MAIN_L0_MS, MAIN_L1_MS, now - layer_anim);
    if (r1 == REVEAL_HIDDEN) {
        oled_write_line_plain(PSTR(""), 1, false);
    } else {
        oled_write_info_line(1, PSTR("LAYER:"), layer_names[layer], r1, false);
    }

    uint8_t r2 = info_reveal(tm, MAIN_L1_MS, MAIN_L2_MS, now - caps_anim);
    if (r2 == REVEAL_HIDDEN) {
        oled_write_line_plain(PSTR(""), 2, false);
    } else {
        oled_write_info_line(2, PSTR("CAPSLOCK:"), caps ? PSTR("ON") : PSTR("OFF"), r2, false);
    }

    uint8_t r3 = info_reveal(tm, MAIN_L2_MS, MAIN_L3_MS, now - mode_anim);
    if (r3 == REVEAL_HIDDEN) {
        oled_write_line_plain(PSTR(""), 3, false);
    } else {
        oled_write_info_line(3, PSTR("MODE:"), mac ? PSTR("MAC") : PSTR("WIN"), r3, false);
    }
}

static const char skills[][19] PROGMEM = {"BRAINDANCE LOAD...", "QUICKHACK", "NETRUNNER", "JACK IN", "ICEBREAK", "DAEMON UPLOAD", "RELIC DETECTED", "TRACE PROTOCOL", "FLATLINE", "SHARD COMPLETE"};
#define NUM_SKILLS (sizeof(skills) / sizeof(skills[0]))

enum skill_state { SKILL_TYPE, SKILL_BAR, SKILL_LOADED, SKILL_CLEAR };

#define SKILL_BAR_MS    1200
#define SKILL_LOADED_MS 1200
#define SKILL_CLEAR_MS   250
#define LOADED_BLINK_MS  300

static enum skill_state skill_st          = SKILL_TYPE;
static uint32_t         skill_state_start = 0;
static uint8_t          skill_idx         = 0;
static uint8_t          typed             = 0;
static uint32_t         last_activity     = 0;
static uint8_t          activity_parity   = 0;

// Ma trận đổi cả lúc nhấn lẫn lúc nhả, nên đếm parity để một phím ra một ký tự.
static void skill_count_keys(void) {
    uint32_t a = last_matrix_activity_time();
    if (a == last_activity) return;
    last_activity = a;
    activity_parity ^= 1;
    if (activity_parity == 0) typed++;
}

static void render_right_main(uint32_t now) {
    uint8_t len = vstrlen(skills[skill_idx]);

    switch (skill_st) {
        case SKILL_TYPE:
            skill_count_keys();
            if (typed >= len) {
                skill_st          = SKILL_BAR;
                skill_state_start = now;
            }
            break;
        case SKILL_BAR:
            if (now - skill_state_start >= SKILL_BAR_MS) {
                skill_st          = SKILL_LOADED;
                skill_state_start = now;
            }
            break;
        case SKILL_LOADED:
            if (now - skill_state_start >= SKILL_LOADED_MS) {
                skill_st          = SKILL_CLEAR;
                skill_state_start = now;
            }
            break;
        case SKILL_CLEAR:
            if (now - skill_state_start >= SKILL_CLEAR_MS) {
                skill_idx = (skill_idx + 1) % NUM_SKILLS;
                typed     = 0;
                skill_st  = SKILL_TYPE;
            }
            break;
    }

    // Dòng 0 luôn trống: band trống tạo tương phản cho frame jump của glitch.
    oled_write_line_plain(PSTR(""), 0, false);

    if (skill_st == SKILL_CLEAR) {
        oled_write_line_plain(PSTR(""), 1, false);
        oled_write_line_plain(PSTR(""), 2, false);
        oled_write_line_plain(PSTR(""), 3, false);
        return;
    }

    oled_write_line_type(skills[skill_idx], 1, typed, false);

    if (skill_st == SKILL_TYPE) {
        oled_write_line_plain(PSTR(""), 2, false);
        oled_write_line_plain(PSTR(""), 3, false);
        return;
    }

    uint32_t st   = now - skill_state_start;
    uint8_t  fill = LINE_COLS;
    if (skill_st == SKILL_BAR) {
        fill = (uint8_t)(st * LINE_COLS / SKILL_BAR_MS);
        if (fill > LINE_COLS) fill = LINE_COLS;
    }
    oled_write_bar_line(2, fill);

    if (skill_st == SKILL_LOADED && ((st / LOADED_BLINK_MS) & 1) == 0) {
        oled_write_loaded_line(3);
    } else {
        oled_write_line_plain(PSTR(""), 3, false);
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

        // ~1/10 band: dropout cả band. Tạo tương phản band trống/band đầy nên
        // frame jump và noise bar nổi rõ cả trên màn có 4 dòng text đều nhau.
        if ((prng_next() % 10) == 0) {
            for (uint8_t i = 0; i < 128; i++) band[i] = 0;
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
            phase      = PHASE_MAIN;
            main_start = now;
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
