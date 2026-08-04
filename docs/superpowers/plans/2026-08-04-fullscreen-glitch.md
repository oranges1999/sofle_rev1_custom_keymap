# Full-screen Glitch + Tearing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Thay hiệu ứng glitch định kỳ hiện tại bằng full-screen glitch + tearing thao tác trực tiếp buffer pixel SSD1306 trên cả 2 màn.

**Architecture:** Sau khi render nội dung mỗi frame (MAIN), gọi `oled_apply_glitch()` để biến dạng buffer 512 byte (4 band × 128 byte): dịch ngang từng band 0-6px (tearing), đôi lúc nhân đôi band (frame jump), phủ noise bars — mỗi 2500ms burst 300ms. Gỡ bỏ hoàn toàn `glitch_line_state()` cũ.

**Tech Stack:** QMK Firmware, C (AVR/ATmega32U4), SSD1306 OLED I2C, API `oled_read_raw`/`oled_write_raw`.

**Spec:** `docs/superpowers/specs/2026-08-04-fullscreen-glitch-design.md`

## Global Constraints

- Target build: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
- Flash budget: giữ < 25,000 bytes (limit 28,672; baseline ~23,636).
- Chỉ sửa `oled_cyberdeck.c`; không sửa driver OLED.
- Không dùng `wait_ms()` trong render — chỉ dựa `timer_read32()`.
- Buffer 512 byte = 4 band × 128 byte; 1 byte = 1 cột × 8 pixel dọc.
- `oled_set_cursor(0, page)` → index `page * 128`; `oled_write_raw(data, size)` ghi tại cursor, tự đánh dirty.
- Git commit: chỉ thực hiện nếu người dùng yêu cầu rõ ràng.
- Không có framework test — mỗi task verify bằng `qmk compile` (build OK + size).

---

### Task 1: Gỡ bỏ hiệu ứng glitch cũ, đơn giản hóa `oled_write_line_final`

**Files:**
- Modify: `oled_cyberdeck.c:27-64`

**Interfaces:**
- Consumes: không (chỉ xóa + đơn giản hóa nội bộ).
- Produces: `oled_write_line_final(uint8_t line, char *buf, bool invert)` — luôn ghi full line tại cột 0 (không còn nhánh dịch/rác). Tất cả hàm gọi nó giữ nguyên chữ ký.

- [ ] **Step 1: Xóa block glitch cũ**

Xóa toàn bộ khối sau (từ comment `// ---- Hiệu ứng glitch toàn màn hình (định kỳ) ----` đến hết `glitch_line_state`):

```c
// ---- Hiệu ứng glitch toàn màn hình (định kỳ) ----
static uint32_t next_glitch     = 0;
static uint32_t glitch_start    = 0;
static uint32_t glitch_end      = 0;
static uint8_t  glitch_line_sel = 0;

#define GLITCH_PERIOD_MS 1800
#define GLITCH_BURST_MS  150

static uint8_t glitch_line_state(uint8_t line) {
    uint32_t now = timer_read32();
    if (now >= next_glitch) {
        glitch_start = now;
        glitch_end = now + GLITCH_BURST_MS;
        next_glitch = now + GLITCH_PERIOD_MS;
        glitch_line_sel = prng_next() % 4;
    }
    if (now >= glitch_end) return 0;
    if (line == glitch_line_sel) return 2;
    return 1;
}
```

- [ ] **Step 2: Đơn giản hóa `oled_write_line_final`**

Thay toàn bộ hàm:

```c
static void oled_write_line_final(uint8_t line, char *buf, bool invert) {
    uint8_t gs = glitch_line_state(line);
    if (gs == 1) {
        oled_set_cursor(1, line);
        oled_write(&buf[1], false);
    } else if (gs == 2) {
        for (uint8_t k = 0; k < 3; k++) {
            buf[prng_next() % (LINE_COLS - 1)] = glitch_char();
        }
        oled_set_cursor(0, line);
        oled_write(buf, false);
    } else {
        oled_set_cursor(0, line);
        oled_write(buf, invert);
    }
}
```

bằng:

```c
static void oled_write_line_final(uint8_t line, char *buf, bool invert) {
    oled_set_cursor(0, line);
    oled_write(buf, invert);
}
```

- [ ] **Step 3: Compile verify**

Run: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Expected: build OK, size < 25,000 bytes (không còn cảnh báo unused).

---

### Task 2: Thêm `oled_apply_glitch()` + gắn vào luồng render

**Files:**
- Modify: `oled_cyberdeck.c` (thêm hàm mới trước `oled_task_user_impl`; sửa `oled_task_user_impl` để gọi)

**Interfaces:**
- Consumes: `prng_next()` (Task 1 giữ nguyên), `timer_read32()`, `oled_read_raw`, `oled_write_raw`, `oled_set_cursor`.
- Produces: `static void oled_apply_glitch(void)` — gọi sau mỗi frame MAIN, tự kiểm tra nhịp burst, biến dạng buffer cả 4 band; `static uint32_t next_glitch`, `static uint32_t glitch_end`, `#define GLITCH_PERIOD_MS 2500`, `#define GLITCH_BURST_MS 300`.

- [ ] **Step 1: Thêm hàm `oled_apply_glitch`**

Chèn khối sau vào trước `static void oled_task_user_impl(void)`:

```c
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
```

- [ ] **Step 2: Gắn gọi `oled_apply_glitch` sau khi render MAIN**

Sửa `oled_task_user_impl` — nhánh MAIN hiện tại:

```c
    oled_clear();
    if (is_keyboard_master()) {
        render_left_main(now);
    } else {
        render_right_main(now);
    }
```

thành:

```c
    oled_clear();
    if (is_keyboard_master()) {
        render_left_main(now);
    } else {
        render_right_main(now);
    }
    oled_apply_glitch();
```

- [ ] **Step 3: Compile verify**

Run: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
Expected: build OK, size < 25,000 bytes, không có cảnh báo `-Wunused`.

---

### Task 3: Flash & kiểm tra phần cứng

**Files:** không đổi code.

- [ ] **Step 1: Flash cả 2 nửa**

Run: `qmk flash -kb sofle/rev1 -km sofle_rev1_custom_keymap` (flash từng nửa, mỗi nửa reset vào bootloader riêng).

- [ ] **Step 2: Kiểm tra checklist**

- Cứ ~2.5s có 1 burst ~300ms: các band (4 dải ngang) dịch trái/phải độc lập 0-6px → xuất hiện đường xé ngang giữa các band.
- Thỉnh thoảng (mỗi vài burst) có hiệu ứng nhân đôi band (frame jump) và noise bar dọc.
- Giữa các burst, nội dung chữ đọc rõ: BRAINDANCE... (màn phải), layer/caps/mode (màn trái).
- Cả 2 màn đều glitch (mỗi nửa độc lập, gần đồng bộ thời gian).
- Gõ phím → hex feedback dòng 3 màn phải vẫn hoạt động; gõ liên tục không lag.
- Không còn hiệu ứng cũ (chỉ dịch 1 dòng chữ + vài ký tự rác trong 150ms).

- [ ] **Step 3: Nếu cần tinh chỉnh cường độ** — chỉnh `#define GLITCH_PERIOD_MS` (khoảng cách burst), `GLITCH_BURST_MS` (độ dài), hoặc `% 7` (dịch tối đa px) rồi flash lại.
