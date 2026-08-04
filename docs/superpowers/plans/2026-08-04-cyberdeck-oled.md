# Cyberdeck OLED Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Thêm hiển thị OLED phong cách cyberpunk (boot animation + màn trái thông tin + màn phải glitch text cycler + idle/wake + hex feedback) cho keymap Sofle Rev1.

**Architecture:** Tách toàn bộ logic OLED vào 2 file mới `oled_cyberdeck.c/.h`; `keymap.c` chỉ include header và gọi `cyberdeck_key_pressed()` từ `process_record_user`. Bật OLED qua `rules.mk` (`OLED_ENABLE`, `OLED_DRIVER=ssd1306`, `SRC += oled_cyberdeck.c`) và `config.h` (`OLED_DISPLAY_128X32`, `SPLIT_OLED_ENABLE`). Rotation 90/270 → lưới 5 cột × 16 dòng.

**Tech Stack:** QMK Firmware, C (AVR/ATmega32U4), SSD1306 OLED I2C, không dùng thư viện ngoài.

## Global Constraints

- Target build: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
- Flash budget: phải giữ < 25,000 bytes (limit 28,672); baseline hiện tại ~21,016.
- Font QMK mặc định **6x8**; với rotation 90/270: `oled_max_chars()`=5, `oled_max_lines()`=16.
- Glyph `█`/`░` (0xDB/0xB0) KHÔNG có trong font → progress bar dùng `#` (đầy) và `-` (trống).
- Tất cả chuỗi text cố định đặt PROGMEM, đọc bằng `pgm_read_byte`.
- Không dùng `wait_ms()` trong render — chỉ tính bằng `timer_read32()`.
- Không dùng `snprintf`/`printf` (tốn flash) — hex build thủ công.
- Gọi `oled_clear()` đầu mỗi frame để tránh chữ cũ bám lại (memset 512B rẻ).
- Git commit: chỉ thực hiện nếu người dùng yêu cầu rõ ràng.

---

### Task 1: Bật OLED + khung xương (skeleton)

**Files:**
- Modify: `rules.mk`
- Modify: `config.h`
- Create: `oled_cyberdeck.h`
- Create: `oled_cyberdeck.c`
- Test: compile

**Interfaces:**
- Produces: `void cyberdeck_key_pressed(void)` (khai báo trong header, dùng ở Task 6)

- [ ] **Step 1: Sửa `rules.mk`** — thêm 3 dòng cuối:

```make
TRI_LAYER_ENABLE = yes
ENCODER_MAP_ENABLE = yes
MOUSEKEY_ENABLE = yes
OLED_ENABLE = yes
OLED_DRIVER = ssd1306
SRC += oled_cyberdeck.c
```

- [ ] **Step 2: Sửa `config.h`** — thêm vào cuối file:

```c
#define OLED_DISPLAY_128X32
#define SPLIT_OLED_ENABLE
```

- [ ] **Step 3: Tạo `oled_cyberdeck.h`:**

```c
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

void cyberdeck_key_pressed(void);
```

- [ ] **Step 4: Tạo `oled_cyberdeck.c` (skeleton, test hiển thị):**

```c
// SPDX-License-Identifier: GPL-2.0-or-later
#include "quantum.h"
#include "oled_cyberdeck.h"

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    if (is_keyboard_master()) {
        return OLED_ROTATION_270;
    }
    return OLED_ROTATION_90;
}

bool oled_task_user(void) {
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_P(PSTR("CYBER"), false);
    oled_set_cursor(0, 1);
    oled_write_P(PSTR("DECK"), false);
    return true;
}
```

- [ ] **Step 5: Compile để xác nhận**

Run: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Expected: build OK, size < 25,000 bytes.

- [ ] **Step 6: Commit (tùy chọn — hỏi người dùng trước khi commit)**

```bash
git add rules.mk config.h oled_cyberdeck.h oled_cyberdeck.c
git commit -m "feat(oled): enable ssd1306 oled skeleton for sofle"
```

---

### Task 2: PRNG + bộ helper text dọc

**Files:**
- Modify: `oled_cyberdeck.c`

**Interfaces:**
- Produces (static, dùng trong Task 3-5):
  - `static uint32_t prng_next(void)`
  - `static char glitch_char(void)`
  - `static uint8_t vstrlen(const char *text)` — độ dài chuỗi PROGMEM
  - `static uint8_t oled_write_vertical(const char *text, uint8_t start, bool invert)` — trả line kế tiếp
  - `static uint8_t oled_write_vertical_glitched(const char *text, uint8_t start, uint8_t reveal, bool invert)`
  - `static uint8_t oled_write_vertical_flicker(const char *text, uint8_t start, uint8_t flicker_pos, bool invert)`
  - `static void oled_write_vertical_bar(uint8_t start, uint8_t filled)` — filled 0..15
  - `static void oled_clear_lines(uint8_t start, uint8_t count)`

- [ ] **Step 1: Thay toàn bộ nội dung `oled_cyberdeck.c`** bằng phiên bản gồm PRNG + helpers (giữ `oled_init_user`/`oled_task_user` từ Task 1):

```c
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

static void oled_clear_lines(uint8_t start, uint8_t count) {
    for (uint8_t l = 0; l < count; l++) {
        oled_set_cursor(0, start + l);
        oled_write_P(PSTR("     "), false);
    }
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

bool oled_task_user(void) {
    oled_clear();
    oled_write_vertical(PSTR("HELLO"), 0, false);
    return true;
}
```

- [ ] **Step 2: Compile**

Run: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Expected: build OK, size < 25,000 bytes.

- [ ] **Step 3: Commit (tùy chọn — hỏi người dùng trước khi commit)**

```bash
git add oled_cyberdeck.c
git commit -m "feat(oled): add prng and vertical text helpers"
```

---

### Task 3: Boot animation + máy trạng thái

**Files:**
- Modify: `oled_cyberdeck.c`

**Interfaces:**
- Consumes: `prng_next`, `glitch_char`, `vstrlen`, `oled_write_vertical_glitched`, `oled_write_vertical_bar` (Task 2)
- Produces: `enum cyber_phase { PHASE_BOOT, PHASE_MAIN }`, `enum main_state { MAIN_NORMAL, MAIN_SHUTDOWN, MAIN_SCREEN_OFF, MAIN_WAKE }`, `static void oled_task_user_impl(void)`, biến static `phase`, `mstate`, `boot_start` (dùng cho Task 4-6)

- [ ] **Step 1: Thay hàm `oled_task_user` + thêm state machine và `render_boot`**

Thay block `bool oled_task_user(void) { ... }` cuối file bằng:

```c
enum cyber_phase { PHASE_BOOT, PHASE_MAIN };
enum main_state  { MAIN_NORMAL, MAIN_SHUTDOWN, MAIN_SCREEN_OFF, MAIN_WAKE };

static enum cyber_phase phase = PHASE_BOOT;
static enum main_state  mstate = MAIN_NORMAL;

static uint32_t boot_start = 0;

#define BOOT_TOTAL_MS  4000
#define BOOT_ONLINE_MS 3400
#define BAR_UNITS      15

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

    (void)mstate; // will be used in Task 6
}

bool oled_task_user(void) {
    oled_clear();
    oled_task_user_impl();
    return true;
}
```

- [ ] **Step 2: Compile**

Run: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Expected: build OK (có thể có cảnh báo unused `mstate` — chấp nhận, sẽ dùng ở Task 6).

- [ ] **Step 3: Commit (tùy chọn — hỏi người dùng trước khi commit)**

```bash
git add oled_cyberdeck.c
git commit -m "feat(oled): boot animation and phase state machine"
```

---

### Task 4: Màn trái — thông tin

**Files:**
- Modify: `oled_cyberdeck.c`

**Interfaces:**
- Consumes: `phase`, `boot_start`, `oled_clear`, helpers Task 2
- Produces: `static void render_left_main(uint32_t now)` (dùng Task 6 dispatcher)
- Sử dụng API QMK: `get_highest_layer`, `host_keyboard_led_state().caps_lock`, `keymap_config.swap_lctl_lgui`, `is_keyboard_master`

- [ ] **Step 1: Thêm helper `oled_write_glitched_line` + hàm `render_left_main` vào `oled_cyberdeck.c` (trước `oled_task_user_impl`)**

Helper glitch 1 dòng ngang (không wrap), dùng cho WIN/MAC:

```c
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
```

Hàm render chính:

```c
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
```

- [ ] **Step 2: Gắn vào dispatcher** — sửa `oled_task_user_impl` để render màn trái khi vào MAIN:

```c
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

    if (is_keyboard_master()) {
        render_left_main(now);
    }
}
```

- [ ] **Step 3: Compile**

Run: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Expected: build OK, size < 25,000 bytes.

- [ ] **Step 4: Commit (tùy chọn — hỏi người dùng trước khi commit)**

```bash
git add oled_cyberdeck.c
git commit -m "feat(oled): left screen layer/caps/mode info"
```

---

### Task 5: Màn phải — glitch text cycler

**Files:**
- Modify: `oled_cyberdeck.c`

**Interfaces:**
- Consumes: helpers Task 2, `phase`, `boot_start`
- Produces: `static void render_right_main(uint32_t now)` (dùng Task 6 dispatcher)

- [ ] **Step 1: Thêm danh sách câu + hàm `render_right_main` vào `oled_cyberdeck.c`**

```c
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
}
```

- [ ] **Step 2: Gắn vào dispatcher** — sửa nhánh MAIN của `oled_task_user_impl`:

```c
    if (is_keyboard_master()) {
        render_left_main(now);
    } else {
        render_right_main(now);
    }
```

- [ ] **Step 3: Compile**

Run: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Expected: build OK, size < 25,000 bytes.

- [ ] **Step 4: Commit (tùy chọn — hỏi người dùng trước khi commit)**

```bash
git add oled_cyberdeck.c
git commit -m "feat(oled): right screen glitch text cycler"
```

---

### Task 6: Idle/wake + hex feedback + nối vào keymap.c

**Files:**
- Modify: `oled_cyberdeck.c`
- Modify: `oled_cyberdeck.h` (không đổi)
- Modify: `keymap.c`

**Interfaces:**
- Consumes: `cyberdeck_key_pressed` (Task 1), `render_left_main`, `render_right_main`, `phase`, `mstate`
- Produces: hoàn thiện `oled_task_user_impl` đầy đủ trạng thái MAIN

- [ ] **Step 1: Thêm biến idle/hex + hoàn thiện state machine trong `oled_cyberdeck.c`**

Thêm sau khối `enum main_state` / trước `render_boot`:

```c
static uint32_t last_input     = 0;
static uint32_t shutdown_start = 0;
static uint32_t wake_start     = 0;
static uint32_t hex_start      = 0;
static uint8_t  hex_val        = 0;

#define IDLE_TIMEOUT_MS   120000
#define SHUTDOWN_ANIM_MS  500
#define WAKE_ANIM_MS      500
#define HEX_ANIM_MS       1000
```

Thêm helper hex (sau `oled_write_glitched_line`):

```c
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
```

- [ ] **Step 2: Thêm hex feedback vào cuối `render_right_main`** (sau block progress bar):

```c
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
```

- [ ] **Step 3: Hoàn thiện `oled_task_user_impl`** (thay toàn bộ):

```c
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
```

- [ ] **Step 4: Thêm `cyberdeck_key_pressed` vào `oled_cyberdeck.c`** (cuối file, sau `oled_task_user`):

```c
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
```

- [ ] **Step 5: Nối vào `keymap.c`** — thêm include đầu file và hook đầu `process_record_user`:

```c
#include "oled_cyberdeck.h"
```

Sửa hàm `process_record_user` (thêm 3 dòng đầu):

```c
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        cyberdeck_key_pressed();
    }
    switch (keycode) {
        ...
```

- [ ] **Step 6: Compile**

Run: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Expected: build OK, size < 25,000 bytes.

- [ ] **Step 7: Commit (tùy chọn — hỏi người dùng trước khi commit)**

```bash
git add oled_cyberdeck.c keymap.c
git commit -m "feat(oled): idle wake and keystroke hex feedback"
```

---

### Task 7: Flash & kiểm tra phần cứng

**Files:** không đổi code (chỉ test + tinh chỉnh số liệu nếu cần)

- [ ] **Step 1: Flash bàn phím**

Run: `qmk flash -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Flash lần lượt từng nửa (mỗi nửa reset vào bootloader riêng). Xem `qmk flash --help` nếu cần chọn bootloader.

- [ ] **Step 2: Kiểm tra checklist trên cả 2 nửa**

- Rotation: text đọc trái→phải trên cả 2 màn. Nếu màn nào lộn ngược → hoán đổi `OLED_ROTATION_270`/`90` trong `oled_init_user` rồi flash lại.
- Boot: glitch-in CYBERDECK/INITIATING → bar dọc đầy → SYSTEM ONLINE → vào UI.
- Đổi layer (phím LOWER/RAISE): màn trái đổi tên layer + hiệu ứng glitch.
- Bật/tắt Caps Lock: hiện `ON`/trống.
- CG_TOGG trên layer ADJUST: `WIN`↔`MAC`.
- Gõ liên tục: màn phải có hex ở dòng 15, không lag gõ.
- Để yên 2 phút: `SYSTEM OFFLINE` → tắt. Gõ phím: `REBOOTING` → về UI.
- Chỉ cắm 1 nửa: vẫn chạy bình thường trên nửa đó.

- [ ] **Step 3: Nếu cần tinh chỉnh** — chỉnh các hằng số `#define` (BOOT_TOTAL_MS, IDLE_TIMEOUT_MS, CYCLE_MS...) và flash lại.
