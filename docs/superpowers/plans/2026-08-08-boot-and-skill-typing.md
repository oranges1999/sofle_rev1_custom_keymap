# Boot tuần tự + màn phải theo nhịp gõ — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Đổi màn boot của cả hai nửa sang một chuỗi tuần tự (gõ chữ → thanh progress → xoá màn → nhấp nháy `SYSTEM ONLINE` → load màn chính từng dòng), và đổi màn phải từ vòng lặp theo thời gian sang vòng lặp điều khiển bởi nhịp gõ phím.

**Architecture:** Toàn bộ hiển thị nằm trong một file `oled_cyberdeck.c` như hiện tại. Chuỗi boot thuần tuyến tính theo thời gian nên chỉ cần thêm mốc trong `render_boot()`, không thêm state. Màn phải cần một state machine bốn trạng thái vì nhịp của nó do người dùng điều khiển. Sự kiện phím tới được nửa slave nhờ `SPLIT_ACTIVITY_ENABLE` của QMK. Logic dựng buffer được kiểm bằng một test biên dịch trên máy phát triển với bộ stub API QMK.

**Tech Stack:** QMK Firmware, C99, AVR ATmega32U4 (avr-gcc), OLED SSD1306 128x32 qua I2C, split keyboard transport. Test host dùng `gcc` và `assert.h`, không framework.

## Global Constraints

- Spec nguồn: `docs/superpowers/specs/2026-08-08-boot-and-skill-typing-design.md`. Mọi con số thời gian lấy nguyên từ spec.
- Không dùng `wait_ms()` hay bất kỳ hàm chặn nào trong đường render. Chỉ dùng `timer_read32()` và so mốc.
- Không sửa driver OLED trong `drivers/oled/`.
- Kích thước firmware phải dưới 28672 byte. Mốc trước khi bắt đầu: 24072 byte (83%). Nếu vượt 95% thì dừng lại báo cáo trước khi đi tiếp.
- `LINE_COLS` là 21. Mọi dòng ghi ra OLED phải đúng 21 ký tự cộng một `'\0'` ở `buf[21]`.
- Chuỗi hằng phải nằm trong PROGMEM (`PSTR(...)` hoặc mảng khai báo `PROGMEM`) và đọc bằng `pgm_read_byte()`. Đây là AVR, đọc thẳng con trỏ PROGMEM sẽ ra rác.
- Comment trong code viết bằng tiếng Việt, theo đúng phong cách các comment sẵn có trong `oled_cyberdeck.c`.
- Thư mục làm việc của mọi lệnh trong plan này là `/home/rb070/qmk_firmware/keyboards/sofle/keymaps/sofle_rev1_custom_keymap` trừ khi ghi rõ khác.
- Lệnh biên dịch firmware chạy từ `/home/rb070/qmk_firmware`: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`
- `opencode.md` ghi "KHÔNG tự `git commit` khi chưa được yêu cầu". Việc user duyệt plan này chính là sự cho phép commit theo từng task dưới đây, và **chỉ** những commit đó. Không commit gì ngoài phạm vi các task.
- Nạp firmware (`qmk flash`) là việc của user, không tự chạy.
- Nhánh làm việc: `main`.

---

## File Structure

| File                | Trạng thái                          | Trách nhiệm                                                   |
| ------------------- | ----------------------------------- | ------------------------------------------------------------- |
| `oled_cyberdeck.c`  | Sửa                                 | Toàn bộ logic hiển thị OLED hai màn                           |
| `oled_cyberdeck.h`  | Xoá ở Task 4                        | Chỉ khai báo `cyberdeck_key_pressed()`, hàm này bị bỏ         |
| `keymap.c`          | Sửa ở Task 4                        | Bỏ lời gọi `cyberdeck_key_pressed()` và include header đã xoá |
| `config.h`          | Sửa ở Task 4                        | Thêm `SPLIT_ACTIVITY_ENABLE`                                  |
| `tests/quantum.h`   | Tạo ở Task 1                        | Stub API QMK để biên dịch `oled_cyberdeck.c` trên host        |
| `tests/test_line.c` | Tạo ở Task 1, mở rộng ở Task 3 và 4 | Test host cho logic dựng buffer và state machine              |
| `tests/run.sh`      | Tạo ở Task 1                        | Một dòng biên dịch và chạy test                               |

`tests/` chỉ dùng cho test host. QMK không biên dịch thư mục này vì `rules.mk` chỉ khai báo `SRC += oled_cyberdeck.c`.

---

### Task 1: Bộ test host và kiểu hiển thị decode

Đổi ngữ nghĩa của tham số `reveal` từ "loạn toàn bộ phần chưa hiện" sang "decode từng ký tự", và dựng bộ test host để khoá hành vi này lại.

**Files:**

- Create: `tests/quantum.h`
- Create: `tests/test_line.c`
- Create: `tests/run.sh`
- Modify: `oled_cyberdeck.c` (hàm `oled_write_line_full` đổi tên và đổi hành vi, `oled_write_info_line`, các nơi gọi trong `render_boot` và `render_right_main`)

**Interfaces:**

- Consumes: không có, đây là task đầu.
- Produces:
  - `static void oled_write_line_type(const char *text, uint8_t line, uint8_t reveal, bool invert)` — ghi một dòng 21 cột với tiền tố `"> "`. Vị trí `i` trong chuỗi: `i < reveal` ghi ký tự thật, `i == reveal` ghi `glitch_char()`, `i > reveal` ghi khoảng trắng. Tham số `cursor` của hàm cũ bị bỏ.
  - `static void oled_write_info_line(uint8_t line, const char *label, const char *value, uint8_t reveal, bool invert)` — giữ nguyên chữ ký, phần value đổi sang quy tắc decode như trên.
  - `tests/run.sh` — chạy toàn bộ test host, thoát mã 0 khi mọi assert qua.

- [ ] **Step 1: Tạo bộ stub API QMK**

Tạo `tests/quantum.h`:

```c
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
```

- [ ] **Step 2: Viết test thất bại cho `oled_write_line_type`**

Tạo `tests/test_line.c`:

```c
// Test host cho logic dựng buffer của oled_cyberdeck.c.
// Chạy bằng tests/run.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../oled_cyberdeck.c"

// So dòng đã ghi ra màn với chuỗi mong đợi. Chuỗi mong đợi được tự đệm khoảng
// trắng cho đủ 21 cột, khỏi phải đếm dấu cách bằng mắt.
static void expect_line(uint8_t line, const char *want) {
    char padded[22];
    assert(strlen(want) <= 21);
    memset(padded, ' ', 21);
    padded[21] = '\0';
    memcpy(padded, want, strlen(want));
    if (strcmp(stub_screen[line], padded) != 0) {
        printf("dòng %u sai\n  muốn : \"%s\"\n  nhận : \"%s\"\n", line, padded, stub_screen[line]);
        assert(0);
    }
}

static void test_type_reveal_zero(void) {
    oled_write_line_type(PSTR("ABC"), 0, 0, false);
    assert(stub_screen[0][0] == '>');
    assert(stub_screen[0][1] == ' ');
    assert(stub_screen[0][2] != ' '); // ký tự rác đang giải mã
    assert(stub_screen[0][3] == ' ');
    assert(strlen(stub_screen[0]) == 21);
}

static void test_type_reveal_giua(void) {
    oled_write_line_type(PSTR("ABCDE"), 1, 2, false);
    assert(strncmp(stub_screen[1], "> AB", 4) == 0);
    assert(stub_screen[1][4] != ' '); // rác tại vị trí 2 của chuỗi
    assert(stub_screen[1][5] == ' ');
    assert(strlen(stub_screen[1]) == 21);
}

static void test_type_reveal_bang_do_dai(void) {
    oled_write_line_type(PSTR("ABCDE"), 2, 5, false);
    expect_line(2, "> ABCDE");
}

static void test_type_reveal_vuot_do_dai(void) {
    oled_write_line_type(PSTR("ABCDE"), 3, 99, false);
    expect_line(3, "> ABCDE");
}

static void test_type_chuoi_dai_bi_cat(void) {
    oled_write_line_type(PSTR("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), 0, 99, false);
    expect_line(0, "> ABCDEFGHIJKLMNOPQRS");
}

static void test_info_line_day_du(void) {
    oled_write_info_line(1, PSTR("LAYER:"), PSTR("QWERTY"), 6, false);
    expect_line(1, "LAYER:    QWERTY");
}

static void test_info_line_dang_decode(void) {
    oled_write_info_line(2, PSTR("MODE:"), PSTR("WIN"), 1, false);
    assert(strncmp(stub_screen[2], "MODE:     W", 11) == 0);
    assert(stub_screen[2][11] != ' '); // rác tại vị trí 1 của value
    assert(stub_screen[2][12] == ' ');
    assert(strlen(stub_screen[2]) == 21);
}

int main(void) {
    test_type_reveal_zero();
    test_type_reveal_giua();
    test_type_reveal_bang_do_dai();
    test_type_reveal_vuot_do_dai();
    test_type_chuoi_dai_bi_cat();
    test_info_line_day_du();
    test_info_line_dang_decode();
    printf("tất cả test qua\n");
    return 0;
}
```

Tạo `tests/run.sh`:

```sh
#!/bin/sh
# Biên dịch và chạy test host cho oled_cyberdeck.c.
set -e
cd "$(dirname "$0")/.."
gcc -std=c99 -Wall -Wno-unused-function -Wno-unused-variable -Itests -o /tmp/test_line tests/test_line.c
/tmp/test_line
```

Cấp quyền chạy: `chmod +x tests/run.sh`

- [ ] **Step 3: Chạy test để xác nhận nó hỏng**

Run: `./tests/run.sh`

Expected: FAIL khi biên dịch, với lỗi đại ý `implicit declaration of function 'oled_write_line_type'` hoặc `unknown type name`. Hàm chưa tồn tại nên đây là kết quả đúng.

- [ ] **Step 4: Đổi `oled_write_line_full` thành `oled_write_line_type`**

Trong `oled_cyberdeck.c`, thay toàn bộ hàm `oled_write_line_full` (dòng 46-68 của bản hiện tại) bằng:

```c
// Decode từng ký tự: i < reveal là ký tự thật, i == reveal là ký tự rác đang
// giải mã, i > reveal để trống. Ký tự rác ở vị trí reveal đóng vai trò con trỏ.
static void oled_write_line_type(const char *text, uint8_t line, uint8_t reveal, bool invert) {
    char    buf[LINE_COLS + 1];
    uint8_t len = vstrlen(text);
    buf[0] = '>';
    buf[1] = ' ';
    for (uint8_t i = 0; i < LINE_COLS - 2; i++) {
        if (i >= len) {
            buf[i + 2] = ' ';
        } else if (i < reveal) {
            buf[i + 2] = pgm_read_byte(&text[i]);
        } else if (i == reveal) {
            buf[i + 2] = glitch_char();
        } else {
            buf[i + 2] = ' ';
        }
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, invert);
}
```

- [ ] **Step 5: Đổi phần value của `oled_write_info_line` sang decode**

Trong `oled_cyberdeck.c`, thay vòng lặp value của `oled_write_info_line` (khối `for (; i < LINE_COLS; i++)` hiện đang chứa `buf[i] = (k < reveal) ? c : glitch_char();`) bằng:

```c
    uint8_t vlen = vstrlen(value);
    for (; i < LINE_COLS; i++) {
        uint8_t k = i - INFO_LABEL_W;
        if (k >= vlen) {
            buf[i] = ' ';
        } else if (k < reveal) {
            buf[i] = pgm_read_byte(&value[k]);
        } else if (k == reveal) {
            buf[i] = glitch_char();
        } else {
            buf[i] = ' ';
        }
    }
```

- [ ] **Step 6: Cập nhật các nơi gọi hàm cũ**

Tham số `cursor` đã bị bỏ nên mọi lời gọi phải sửa. Trong `render_boot`, đổi ba lời gọi:

```c
    oled_write_line_type(PSTR("CYBERDECK"), 0, rev0, false);
    oled_write_line_type(PSTR("INITIATING"), 1, rev1, false);
```

và

```c
        oled_write_line_type(PSTR("SYSTEM ONLINE"), 3, 13, true);
```

Trong `render_right_main`, đổi hai lời gọi:

```c
        oled_write_line_type(phrases[phrase_idx], 1, rev, false);
```

(cả chỗ trong nhánh `t < TYPE_MS` lẫn chỗ trong nhánh `else` cuối hàm; biến `cur` và dòng `bool cur = ((now >> 8) & 1);` bị bỏ luôn vì không còn ai dùng)

Ở Task 4 toàn bộ `render_right_main` sẽ được viết lại, đây chỉ là bước giữ cho code biên dịch được.

- [ ] **Step 7: Chạy test để xác nhận nó qua**

Run: `./tests/run.sh`

Expected: PASS, in ra `tất cả test qua`.

- [ ] **Step 8: Biên dịch firmware**

Run: `cd /home/rb070/qmk_firmware && qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`

Expected: `[OK]` ở bước Linking, và dòng `The firmware size is fine - NNNNN/28672`. Ghi lại con số để so ở các task sau.

- [ ] **Step 9: Commit**

```bash
git add tests/quantum.h tests/test_line.c tests/run.sh oled_cyberdeck.c
git commit -m "feat: doi reveal sang kieu decode tung ky tu, them test host"
```

---

### Task 2: Chuỗi boot tuần tự và gộp hàm thanh bar

Boot chuyển từ ba hiệu ứng chạy song song sang năm giai đoạn nối tiếp. Nhân tiện xoá hex feedback (code chết vì slave không bao giờ nhận được sự kiện phím) và rút gọn hàm vẽ thanh bar.

**Files:**

- Modify: `oled_cyberdeck.c` (`oled_write_status_line`, hằng số boot, `render_boot`, `render_right_main`, `oled_task_user_impl`)

**Interfaces:**

- Consumes: `oled_write_line_type(text, line, reveal, invert)` từ Task 1.
- Produces:
  - `static void oled_write_bar_line(uint8_t line, uint8_t fill)` — vẽ thanh bar 21 ký tự, `fill` ký tự đầu là `'#'`, còn lại `'-'`. Không có tiền tố `"> "`. Thay hoàn toàn `oled_write_status_line`.
  - `static uint32_t main_start` — mốc thời gian vào `PHASE_MAIN`, Task 3 dùng.
  - Hằng số `BOOT_T1_MS` 900, `BOOT_T2_MS` 2000, `BOOT_T3_MS` 3500, `BOOT_T4_MS` 3600, `BOOT_TOTAL_MS` 5000, `BOOT_BLINK_MS` 350.

- [ ] **Step 1: Thay `oled_write_status_line` bằng `oled_write_bar_line`**

Trong `oled_cyberdeck.c`, xoá toàn bộ hàm `oled_write_status_line` (kèm mảng `hexd` bên trong) và thay bằng:

```c
// Thanh progress 21 ký tự, không có tiền tố "> ".
static void oled_write_bar_line(uint8_t line, uint8_t fill) {
    char buf[LINE_COLS + 1];
    for (uint8_t i = 0; i < LINE_COLS; i++) {
        buf[i] = (i < fill) ? '#' : '-';
    }
    buf[LINE_COLS] = '\0';
    oled_write_line_final(line, buf, false);
}
```

- [ ] **Step 2: Xoá hex feedback**

Xoá khỏi `oled_cyberdeck.c`:

- Hai biến `static uint32_t hex_start = 0;` và `static uint8_t hex_val = 0;`
- Hằng `#define HEX_ANIM_MS 1000`
- Hằng `#define BAR_UNITS 21` (từ nay dùng thẳng `LINE_COLS`, hai giá trị vốn bằng nhau)
- Toàn bộ thân hàm `cyberdeck_key_pressed()` và bản thân hàm

Trong `render_right_main`, thay khối progress bar cuối hàm (từ `// L3: progress bar + hex feedback` tới hết `}` của nhánh `else`) bằng:

```c
    // L3: progress bar
    uint8_t fill = (uint8_t)(t * LINE_COLS / CYCLE_MS);
    if (fill > LINE_COLS) fill = LINE_COLS;
    oled_write_bar_line(3, fill);
```

Lời gọi `cyberdeck_key_pressed()` trong `keymap.c` và khai báo trong `oled_cyberdeck.h` sẽ được dọn ở Task 4. Ở task này, để hàm vẫn tồn tại thì tạm giữ một thân rỗng:

```c
void cyberdeck_key_pressed(void) {
}
```

- [ ] **Step 3: Đổi các hằng mốc boot**

Trong `oled_cyberdeck.c`, thay khối hằng boot hiện tại (`#define BOOT_TOTAL_MS 4000`, `#define BOOT_ONLINE_MS 3400`, `#define BAR_UNITS 21`) bằng:

```c
#define BOOT_T1_MS    900  // xong "CYBERDECK"
#define BOOT_T2_MS   2000  // xong "INITIATING"
#define BOOT_T3_MS   3500  // thanh progress đầy
#define BOOT_T4_MS   3600  // hết khoảng xoá màn
#define BOOT_TOTAL_MS 5000 // hết nhấp nháy SYSTEM ONLINE, vào màn chính
#define BOOT_BLINK_MS 350  // nửa chu kỳ nhấp nháy SYSTEM ONLINE
```

Thêm biến mốc màn chính, đặt cạnh `static uint32_t boot_start = 0;`:

```c
static uint32_t main_start = 0;
```

- [ ] **Step 4: Viết lại `render_boot`**

Thay toàn bộ hàm `render_boot` bằng:

```c
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
            uint32_t bt   = t - BOOT_T2_MS;
            // Chia làm tròn lên: bt chỉ tới 1499 nên chia thường sẽ chốt ở 20,
            // thanh bar không bao giờ đầy trước lúc xoá màn.
            uint16_t span = BOOT_T3_MS - BOOT_T2_MS;
            uint8_t  fill = (uint8_t)((bt * LINE_COLS + span - 1) / span);
            if (fill > LINE_COLS) fill = LINE_COLS;
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
```

- [ ] **Step 5: Ghi `main_start` khi chuyển sang màn chính**

Trong `oled_task_user_impl`, thêm một dòng vào nhánh chuyển phase:

```c
        if (now - boot_start >= BOOT_TOTAL_MS) {
            phase      = PHASE_MAIN;
            main_start = now;
            oled_clear();
        } else {
```

- [ ] **Step 6: Chạy test host**

Run: `./tests/run.sh`

Expected: PASS. Test của Task 1 không đụng tới boot nên vẫn phải qua nguyên vẹn. Nếu hỏng ở bước biên dịch thì có chỗ nào đó còn gọi `oled_write_status_line` hoặc `BAR_UNITS` chưa dọn.

- [ ] **Step 7: Biên dịch firmware**

Run: `cd /home/rb070/qmk_firmware && qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`

Expected: `[OK]`. Kích thước phải nhỏ hơn hoặc xấp xỉ con số của Task 1 vì task này xoá nhiều hơn thêm.

- [ ] **Step 8: Commit**

```bash
git add oled_cyberdeck.c
git commit -m "feat: chuoi boot tuan tu 5 giai doan, xoa hex feedback chet"
```

---

### Task 3: Màn trái load tuần tự từng dòng sau boot

Sau khi vào `PHASE_MAIN`, bốn dòng của màn trái hiện ra lần lượt từ trên xuống thay vì hiện hết cùng lúc.

**Files:**

- Modify: `oled_cyberdeck.c` (thêm hằng mốc, thêm `info_reveal`, viết lại `render_left_main`)
- Modify: `tests/test_line.c` (thêm test cho `info_reveal`)

**Interfaces:**

- Consumes: `main_start`, `oled_write_line_type()`, `oled_write_bar_line()` từ Task 2.
- Produces:
  - `static uint8_t info_reveal(uint32_t tm, uint16_t from, uint16_t to, uint32_t since_change)` — trả `REVEAL_HIDDEN` (0xFF) khi `tm < from`, trả reveal chạy dần khi `from <= tm < to`, sau đó trả reveal theo animation đổi giá trị, cuối cùng trả `REVEAL_DONE` (bằng `LINE_COLS`).
  - Hằng `MAIN_L0_MS` 400, `MAIN_L1_MS` 650, `MAIN_L2_MS` 900, `MAIN_L3_MS` 1150.

- [ ] **Step 1: Viết test thất bại cho `info_reveal`**

Thêm vào `tests/test_line.c`, đặt trước hàm `main`:

```c
static void test_info_reveal(void) {
    // Chưa tới lượt dòng này thì báo ẩn.
    assert(info_reveal(0, 400, 650, 999999) == REVEAL_HIDDEN);
    assert(info_reveal(399, 400, 650, 999999) == REVEAL_HIDDEN);

    // Trong cửa sổ load thì reveal chạy từ 0 lên.
    assert(info_reveal(400, 400, 650, 999999) == 0);
    assert(info_reveal(649, 400, 650, 999999) > 0);
    assert(info_reveal(649, 400, 650, 999999) <= 8);

    // Qua cửa sổ load, giá trị vừa đổi thì chạy animation đổi giá trị.
    assert(info_reveal(5000, 400, 650, 0) == 0);
    assert(info_reveal(5000, 400, 650, 200) == 5);

    // Qua cửa sổ load, giá trị không đổi lâu rồi thì hiện đầy.
    assert(info_reveal(5000, 400, 650, 300) == REVEAL_DONE);
    assert(info_reveal(5000, 400, 650, 999999) == REVEAL_DONE);
}
```

Thêm lời gọi vào `main`, ngay sau `test_info_line_dang_decode();`:

```c
    test_info_reveal();
```

- [ ] **Step 2: Chạy test để xác nhận nó hỏng**

Run: `./tests/run.sh`

Expected: FAIL khi biên dịch với lỗi đại ý `implicit declaration of function 'info_reveal'` và `'REVEAL_HIDDEN' undeclared`.

- [ ] **Step 3: Thêm hằng mốc và hàm `info_reveal`**

Trong `oled_cyberdeck.c`, thêm ngay trước hàm `render_left_main`:

```c
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
```

Con số 8 là độ dài value dài nhất cộng một (`COLEMAK` bảy ký tự), đủ để reveal chạy hết mọi value.

- [ ] **Step 4: Chạy test để xác nhận nó qua**

Run: `./tests/run.sh`

Expected: PASS, in ra `tất cả test qua`.

- [ ] **Step 5: Viết lại `render_left_main`**

Thay toàn bộ hàm `render_left_main` bằng:

```c
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
```

Lưu ý về lần render đầu tiên: `last_layer` khởi tạo `0xFF` nên khác giá trị thật, animation đổi layer bắn ngay ở frame đầu và `layer_anim` bằng `main_start`. Không sao, vì trong suốt cửa sổ load thì nhánh theo `tm` mới là nhánh quyết định, và tới lúc `tm` vượt `MAIN_L1_MS` thì `now - layer_anim` đã lớn hơn 300 nên trả `REVEAL_DONE`.

- [ ] **Step 6: Chạy test host**

Run: `./tests/run.sh`

Expected: PASS.

- [ ] **Step 7: Biên dịch firmware**

Run: `cd /home/rb070/qmk_firmware && qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`

Expected: `[OK]`, kích thước dưới 28672 byte.

- [ ] **Step 8: Commit**

```bash
git add oled_cyberdeck.c tests/test_line.c
git commit -m "feat: man trai load tuan tu tung dong sau boot"
```

---

### Task 4: Màn phải chạy theo nhịp gõ phím

Bỏ vòng lặp phrase theo thời gian, thay bằng state machine bốn trạng thái do phím gõ điều khiển. Bật `SPLIT_ACTIVITY_ENABLE` để nửa slave nhận được sự kiện phím. Dọn nốt `cyberdeck_key_pressed` và header đi kèm.

**Files:**

- Modify: `config.h`
- Modify: `oled_cyberdeck.c` (viết lại `render_right_main`, thêm state machine, thêm `oled_write_loaded_line`, xoá `oled_write_line_flicker` và `cyberdeck_key_pressed`)
- Modify: `keymap.c:153-157` (bỏ include và lời gọi)
- Delete: `oled_cyberdeck.h`
- Modify: `tests/test_line.c` (thêm test state machine)

**Interfaces:**

- Consumes: `oled_write_line_type()`, `oled_write_bar_line()`, `oled_write_line_plain()`, `vstrlen()`.
- Produces:
  - `enum skill_state { SKILL_TYPE, SKILL_BAR, SKILL_LOADED, SKILL_CLEAR }`
  - `static enum skill_state skill_st`, `static uint32_t skill_state_start`, `static uint8_t skill_idx`, `static uint8_t typed`
  - `static void oled_write_loaded_line(uint8_t line)` — ghi `[ LOADED ]` bắt đầu ở cột 3, phần còn lại khoảng trắng, không có tiền tố `"> "`.
  - Hằng `SKILL_BAR_MS` 1200, `SKILL_LOADED_MS` 1200, `SKILL_CLEAR_MS` 250, `LOADED_BLINK_MS` 300.

- [ ] **Step 1: Viết test thất bại cho state machine**

Thêm vào `tests/test_line.c`, đặt trước hàm `main`:

```c
static void reset_skill_state(void) {
    skill_st          = SKILL_TYPE;
    skill_idx         = 0;
    typed             = 0;
    skill_state_start = stub_timer;
    last_activity     = stub_activity;
    activity_parity   = 0;
}

// Một phím sinh hai lần đổi ma trận: nhấn và nhả.
static void press_key(void) {
    stub_activity++;
    render_right_main(stub_timer);
    stub_activity++;
    render_right_main(stub_timer);
}

static void test_skill_go_du_ten_thi_chay_bar(void) {
    stub_timer = 100000;
    reset_skill_state();
    uint8_t len = vstrlen(skills[0]);
    for (uint8_t i = 0; i < len; i++) {
        press_key();
    }
    assert(typed == len);
    assert(skill_st == SKILL_BAR);
}

static void test_skill_chu_ky_day_du(void) {
    stub_timer = 200000;
    reset_skill_state();
    uint8_t len = vstrlen(skills[0]);
    for (uint8_t i = 0; i < len; i++) {
        press_key();
    }
    assert(skill_st == SKILL_BAR);

    stub_timer += SKILL_BAR_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_LOADED);

    stub_timer += SKILL_LOADED_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_CLEAR);

    stub_timer += SKILL_CLEAR_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_TYPE);
    assert(skill_idx == 1);
    assert(typed == 0);
}

static void test_skill_ngung_go_thi_dung_yen(void) {
    stub_timer = 300000;
    reset_skill_state();
    press_key();
    assert(skill_st == SKILL_TYPE);
    assert(typed == 1);

    // Không gõ thêm, thời gian trôi rất lâu: trạng thái không được tự nhảy.
    stub_timer += 60000;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_TYPE);
    assert(typed == 1);
}

static void test_skill_dong_0_luon_trong(void) {
    stub_timer = 400000;
    reset_skill_state();
    render_right_main(stub_timer);
    // Dòng 0 chỉ có dấu nhắc, phần còn lại là khoảng trắng.
    assert(stub_screen[0][0] == '>');
    assert(strspn(stub_screen[0] + 1, " ") == 20);
    assert(strlen(stub_screen[0]) == 21);
}
```

Thêm lời gọi vào `main`, ngay sau `test_info_reveal();`:

```c
    test_skill_go_du_ten_thi_chay_bar();
    test_skill_chu_ky_day_du();
    test_skill_ngung_go_thi_dung_yen();
    test_skill_dong_0_luon_trong();
```

- [ ] **Step 2: Chạy test để xác nhận nó hỏng**

Run: `./tests/run.sh`

Expected: FAIL khi biên dịch với lỗi đại ý `'skill_st' undeclared`, `'skills' undeclared`, `'SKILL_BAR_MS' undeclared`.

- [ ] **Step 3: Bật `SPLIT_ACTIVITY_ENABLE`**

Thêm vào cuối `config.h`:

```c
// Đồng bộ timestamp hoạt động ma trận từ master sang slave. Không có nó thì
// nửa phải không biết phím nào được gõ, vì process_record_user chỉ chạy trên
// master.
#define SPLIT_ACTIVITY_ENABLE
```

- [ ] **Step 4: Thêm hàm ghi dòng LOADED**

Trong `oled_cyberdeck.c`, thêm ngay sau `oled_write_bar_line`:

```c
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
```

- [ ] **Step 5: Xoá `oled_write_line_flicker`**

Xoá toàn bộ hàm `oled_write_line_flicker` khỏi `oled_cyberdeck.c`. Nơi gọi duy nhất nằm trong `render_right_main`, sẽ biến mất ở bước sau.

- [ ] **Step 6: Viết lại phần màn phải**

Thay khối từ `static const char phrases[][19] PROGMEM = {` tới hết hàm `render_right_main` bằng:

```c
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
```

Cũng xoá các hằng không còn dùng: `#define CYCLE_MS 3000`, `#define TYPE_MS 1000`, `#define GLITCHOUT_T (CYCLE_MS - 250)`.

- [ ] **Step 7: Chạy test để xác nhận nó qua**

Run: `./tests/run.sh`

Expected: PASS, in ra `tất cả test qua`.

- [ ] **Step 8: Dọn `cyberdeck_key_pressed` và header**

Xoá thân hàm rỗng `void cyberdeck_key_pressed(void) { }` khỏi cuối `oled_cyberdeck.c`, và xoá dòng `#include "oled_cyberdeck.h"` ở đầu file.

Xoá file: `git rm oled_cyberdeck.h`

Trong `keymap.c`, xoá dòng `#include "oled_cyberdeck.h"` (dòng 5) và sửa đầu hàm `process_record_user` từ:

```c
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        cyberdeck_key_pressed();
    }
    switch (keycode) {
```

thành:

```c
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
```

- [ ] **Step 9: Chạy lại test host sau khi xoá header**

Run: `./tests/run.sh`

Expected: PASS. Nếu hỏng ở `fatal error: oled_cyberdeck.h: No such file` thì `#include` trong `oled_cyberdeck.c` chưa được xoá.

- [ ] **Step 10: Biên dịch firmware**

Run: `cd /home/rb070/qmk_firmware && qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap`

Expected: `[OK]`, kích thước dưới 28672 byte. Báo cáo con số cuối cùng và phần trăm.

- [ ] **Step 11: Commit**

```bash
git add config.h oled_cyberdeck.c keymap.c tests/test_line.c
git rm --cached oled_cyberdeck.h 2>/dev/null || true
git commit -m "feat: man phai chay theo nhip go phim, bo phrase cycle theo thoi gian"
```

---

### Task 5: Nghiệm thu trên phần cứng

Task này do user thực hiện. Agent chuẩn bị lệnh và bảng kiểm, không tự nạp firmware.

**Files:** không sửa file nào.

**Interfaces:**

- Consumes: firmware đã biên dịch từ Task 4.
- Produces: kết luận đạt hoặc danh sách sai lệch cần sửa.

- [ ] **Step 1: Đưa lệnh nạp cho user**

Báo user chạy, nạp lần lượt cả hai nửa:

```bash
qmk flash -kb sofle/rev1 -km sofle_rev1_custom_keymap
```

- [ ] **Step 2: Đi qua bảng kiểm cùng user**

1. Cắm phím: cả hai màn chạy chuỗi boot khoảng 5 giây. Chữ `CYBERDECK` gõ ra từng ký tự, xong mới tới `INITIATING`, xong mới tới thanh progress. Các bước nối tiếp chứ không chồng lên nhau.
2. Thanh progress đầy thì màn xoá sạch, rồi `SYSTEM ONLINE` nhấp nháy khoảng bốn nhịp.
3. Vào màn chính: màn trái đổ ra bốn dòng lần lượt từ trên xuống trong khoảng một giây.
4. Gõ phím: màn phải hiện dần tên skill, khoảng một ký tự mỗi phím. Gõ ở nửa trái và nửa phải đều có tác dụng.
5. Gõ hết tên skill: thanh progress tự chạy khoảng 1,2 giây, xong thì `[ LOADED ]` nhấp nháy dưới thanh, rồi màn xoá và chờ gõ tiếp cho skill kế.
6. Ngừng gõ giữa chừng: màn phải đứng tại tên skill dở, không tự chạy tiếp.
7. Glitch định kỳ vẫn chạy trên cả hai màn, khoảng 2,5 giây một đợt.
8. Gõ liên tục không thấy trễ phím.

- [ ] **Step 3: Xử lý sai lệch nếu có**

Nếu số ký tự hiện ra lệch nhiều so với số phím gõ, đó là do đồng bộ gộp sự kiện. Đường nâng cấp đã ghi trong spec: thay `SPLIT_ACTIVITY_ENABLE` bằng split transaction riêng qua `SPLIT_TRANSACTION_IDS_USER` truyền một bộ đếm phím. Chỉ làm nếu user thấy khó chịu thật.

Nếu tốc độ một bước nào đó không vừa ý, các núm chỉnh là: `BOOT_T1_MS`, `BOOT_T2_MS`, `BOOT_T3_MS`, `BOOT_T4_MS`, `BOOT_TOTAL_MS`, `BOOT_BLINK_MS`, `MAIN_L0_MS` tới `MAIN_L3_MS`, `SKILL_BAR_MS`, `SKILL_LOADED_MS`, `SKILL_CLEAR_MS`, `LOADED_BLINK_MS`, tất cả nằm trong `oled_cyberdeck.c`.

- [ ] **Step 4: Cập nhật ghi chú phiên làm việc**

Sau khi user xác nhận đạt, cập nhật `opencode.md`: thay mục mô tả màn phải cũ bằng mô tả state machine mới, ghi lại kích thước firmware cuối cùng và trạng thái đã nạp.

```bash
git add opencode.md
git commit -m "docs: cap nhat ghi chu phien lam viec sau khi nghiem thu"
```

---

## Self-Review

**Spec coverage:**

| Yêu cầu trong spec                                               | Task                                               |
| ---------------------------------------------------------------- | -------------------------------------------------- |
| Kiểu decode từng ký tự, bỏ tham số `cursor`                      | Task 1                                             |
| Tiền tố `"> "` giữ nguyên, chuỗi tối đa 19 ký tự                 | Task 1 (test `test_type_chuoi_dai_bi_cat`)         |
| `oled_write_info_line` dùng quy tắc decode                       | Task 1                                             |
| Xoá `oled_write_line_flicker`                                    | Task 4 Step 5                                      |
| Bảng mốc boot 0/900/2000/3500/3600/5000                          | Task 2 Step 3, 4                                   |
| `SYSTEM ONLINE` nhấp nháy bằng đảo `invert`, chu kỳ 350ms        | Task 2 Step 4                                      |
| `BOOT_TOTAL_MS` 4000 đổi thành 5000                              | Task 2 Step 3                                      |
| Màn trái load tuần tự 4 dòng theo mốc 400/650/900/1150           | Task 3                                             |
| Giữ animation decode khi đổi layer, caps, mode                   | Task 3 Step 3 (`info_reveal` nhánh `since_change`) |
| State machine 4 trạng thái màn phải                              | Task 4 Step 6                                      |
| Dòng 0 màn phải luôn trống                                       | Task 4 Step 6, test `test_skill_dong_0_luon_trong` |
| Dòng 2 dùng hàm bar, không tiền tố                               | Task 2 Step 1, Task 4 Step 6                       |
| `[ LOADED ]` ở cột 3, nháy bật/tắt chu kỳ 300ms                  | Task 4 Step 4, 6                                   |
| Đếm phím theo parity, chỉ đếm ở `SKILL_TYPE`                     | Task 4 Step 6                                      |
| `SPLIT_ACTIVITY_ENABLE`                                          | Task 4 Step 3                                      |
| Xoá hex feedback, `cyberdeck_key_pressed`, hằng cycle cũ         | Task 2 Step 2, Task 4 Step 6, 8                    |
| Đổi tên `phrases` sang `skills`, `NUM_PHRASES` sang `NUM_SKILLS` | Task 4 Step 6                                      |
| Giữ nguyên `oled_apply_glitch`, PRNG, `oled_init_user`           | Không task nào đụng tới, đúng chủ ý                |
| Test host cho 5 mốc `reveal`                                     | Task 1 Step 2                                      |
| Tám tiêu chí nghiệm thu phần cứng                                | Task 5 Step 2                                      |
| Ngân sách flash                                                  | Global Constraints, kiểm ở mỗi task                |

Không có mục nào trong spec thiếu task.

**Placeholder scan:** Không có "TBD", "TODO", "tương tự Task N", hay bước nào mô tả suông mà thiếu code. Mọi bước sửa code đều kèm khối code đầy đủ.

**Type consistency:** `oled_write_line_type` giữ nguyên chữ ký bốn tham số ở cả Task 1, 2, 3, 4. `oled_write_bar_line(line, fill)` hai tham số, dùng nhất quán ở Task 2 và 4. `info_reveal` bốn tham số, khai báo ở Task 3 Step 3 và dùng đúng vậy ở Step 5 cùng test Step 1. `REVEAL_HIDDEN` và `REVEAL_DONE` định nghĩa ở Task 3 Step 3, test ở Step 1 dùng đúng tên. Các biến state (`skill_st`, `skill_idx`, `typed`, `skill_state_start`, `last_activity`, `activity_parity`) khai báo ở Task 4 Step 6 và test ở Step 1 truy cập đúng tên đó. `skills` và `NUM_SKILLS` nhất quán. `LINE_COLS` thay `BAR_UNITS` ở mọi nơi từ Task 2 trở đi.
