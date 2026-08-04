# Full-screen Glitch + Tearing cho OLED Sofle

Ngày: 2026-08-04
Keyboard: Sofle Rev1 (split, Pro Micro / ATmega32U4)
Màn hình: 2x OLED I2C SSD1306 128x32 (mỗi nửa 1 màn)
File: `oled_cyberdeck.c` (+ `oled_cyberdeck.h` không đổi)

## Mục tiêu

Thay hiệu ứng glitch định kỳ hiện tại (dịch 1 dòng chữ + làm rác vài ký tự, 150ms
mỗi 1.8s) bằng **full-screen glitch + tearing** ở cường độ mạnh, thao tác trực tiếp
buffer pixel của SSD1306, áp dụng cho **cả 2 màn**.

## Yêu cầu đã chốt với người dùng

- Kích hoạt: **định kỳ tự động** (giống hiện tại) — mỗi 2500ms, burst 300ms.
- Phạm vi: **cả 2 màn** (mỗi nửa tự chạy, độc lập).
- Cường độ: **mạnh** — dịch mạnh, tearing nhiều band, nhiễu dày.
- Kỹ thuật: **buffer pixel** (Cách A) — `oled_read_raw` / `oled_write_raw`.

## Kiến trúc buffer SSD1306 128x32

- Buffer: 512 byte, layout row-major: **4 band ngang × 128 byte** (mỗi band = 8
  pixel dọc; mỗi byte = 1 cột gồm 8 pixel dọc).
- Dịch ngang theo byte = dịch theo từng cột pixel (độ mịn 1px).
- `oled_set_cursor(0, page)` → index = `page * 128` (có thể ghi/đọc chính xác từng band).
- API: `oled_read_raw(start)` trả `oled_buffer_reader_t { uint8_t *current_element; uint16_t remaining_element_count; }`; `oled_write_raw(data, size)` ghi tại cursor, tự đánh dirty block.

## Luồng render (mỗi frame, mỗi nửa)

```
oled_task_user_impl(now)
  ├─ PHASE_BOOT → render_boot
  └─ MAIN → oled_clear()
             ├─ render_left_main(now)  (master)  HOẶC  render_right_main(now) (slave)
             └─ oled_apply_glitch()  ← MỚI: biến dạng buffer vừa render
→ oled_render() đẩy buffer lên màn (driver, không đổi)
```

## Hàm mới: `oled_apply_glitch(void)`

**Trạng thái thời gian (static, dùng chung cho cả 2 màn qua timer mỗi nửa):**

```c
static uint32_t next_glitch = 0;
static uint32_t glitch_end  = 0;

#define GLITCH_PERIOD_MS 2500
#define GLITCH_BURST_MS  300
```

Logic mỗi frame:

1. `now = timer_read32()`; nếu `now >= next_glitch`: `glitch_end = now + GLITCH_BURST_MS; next_glitch = now + GLITCH_PERIOD_MS;`
2. Nếu `now >= glitch_end` → không trong burst, trả về ngay (render bình thường).
3. Trong burst (mỗi frame 50ms có tham số ngẫu nhiên mới):
   - Sinh `shift[4]`, mỗi phần tử `0..6` pixel, hướng ngẫu nhiên trái/phải.
   - Xử lý từng band `p` (0..3) với buffer tạm `uint8_t band[128]`:
     - Đọc band gốc: `oled_read_raw(p * 128)`.
     - Dịch ngang: phải `n` → `band[i] = (i>=n) ? src[i-n] : 0`; trái `n` → `band[i] = (i+n<128) ? src[i+n] : 0`. `n=0` → copy nguyên.
     - **Frame jump** (ngẫu nhiên ~1/8 frame): ghi đè band `p` bằng nội dung band
       `(p+1)%4` hoặc `(p-1)%4` (đã dịch theo `shift` của band nguồn) — tạo giật
       khung / nhân đôi band.
     - **Noise bars** (ngẫu nhiên ~1/4 frame): chọn `start = prng()%100`, `width = 2..9`; ghi đè các cột `[start, start+width)` bằng byte ngẫu nhiên.
     - Ghi lại: `oled_set_cursor(0, p); oled_write_raw(band, 128);`
4. Burst kết thúc → frame tiếp theo render sạch như cũ.

**Hiệu quả:** mỗi band dịch độc lập → đường gãy ngang giữa các band (tearing);
toàn khung nhảy mỗi frame (jitter); cột nhiễu dọc (noise bars).

## Gỡ bỏ

- `glitch_line_state()` + statics `next_glitch`/`glitch_start`/`glitch_end`/
  `glitch_line_sel` (cũ) + `GLITCH_PERIOD_MS`/`GLITCH_BURST_MS` cũ (150/1800).
- Đơn giản hóa `oled_write_line_final`: luôn `oled_set_cursor(0, line)` +
  `oled_write(buf, invert)` — bỏ nhánh dịch dòng (gs==1) và làm rác ký tự (gs==2).

## Giữ nguyên

- Hiệu ứng typing reveal/flicker của chữ (`oled_write_line_full` /
  `oled_write_line_flicker` dùng `glitch_char`) — thuộc animation câu chữ.
- Progress bar + hex feedback (màn phải).
- `oled_clear()` đầu mỗi frame MAIN.
- Không còn idle-off (đã gỡ theo yêu cầu trước đó) — màn luôn bật.

## RAM / Hiệu năng

- Stack thêm: `uint8_t band[128]` + `oled_buffer_reader_t` + biến local ≈ **140 byte**
  (tạm, trong `oled_apply_glitch`) — an toàn với 2.5KB RAM ATmega32U4.
- I2C: transform chỉ ghi lại các band trong burst (300ms mỗi 2.5s) — không đáng kể.
- Không dùng `wait_ms()`; chỉ tính toán theo `timer_read32()`.

## Non-goals

- Không thay đổi boot animation.
- Không thay đổi layout bàn phím.
- Không thêm phím kích hoạt glitch (không được chọn).
- Không thay đổi driver OLED (`drivers/oled/oled_driver.c`).

## Kiểm thử

- `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap` — build OK, flash < 28KB.
- Flash cả 2 nửa, quan sát:
  - Cứ ~2.5s có 1 burst 300ms: các band dịch ngang độc lập, có đường xé giữa band,
    đôi lúc nhảy khung và nhiễu dọc.
  - Nội dung chữ (BRAINDANCE..., layer/caps/mode) vẫn đọc được giữa các burst.
  - Cả 2 màn đều glitch (mỗi nửa chạy độc lập).
  - Hex feedback khi gõ vẫn hoạt động.
  - Bàn phím không lag khi gõ liên tục.
