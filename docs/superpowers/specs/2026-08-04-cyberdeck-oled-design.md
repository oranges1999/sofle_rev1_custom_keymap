# Cyberdeck OLED Animation for Sofle Rev1

Ngày: 2026-08-04
Keyboard: Sofle Rev1 (split, Pro Micro / ATmega32U4)
Màn hình: 2x OLED I2C SSD1306 128x32 (mỗi nửa 1 màn)

## Tổng quan

Hiển thị theo phong cách cyberpunk (như Cyberpunk 2077) trên cả 2 màn hình OLED:
2 màn hoạt động như 2 terminal "load cyberdeck" khi khởi động. Sau khi load xong:

- **Màn trái (master)**: hiển thị thông tin — layer đang dùng, Caps Lock, mode
  QWERTY/Colemak + Mac/Win.
- **Màn phải (slave)**: chạy animation "glitch text cycler" — các câu tiêu biểu
  của Cyberpunk 2077 (BRAINDANCE LOAD..., QUICKHACK, NETRUNNER...) lần lượt
  xuất hiện với hiệu ứng glitch, kèm keystroke hex feedback khi gõ phím.

Tất cả hiệu ứng glitch dùng phông chữ mặc định của QMK (6x8), không dùng bitmap
ngoài.

**Hướng màn & layout:** OLED gắn **dọc** (portrait), muốn text đọc theo chiều ngang
(trái→phải) nên dùng **rotation 90/270** → lưới text hiệu dụng là **5 cột × 16 dòng**
(oled_max_chars = 32/6 = 5, oled_max_lines = 128/8 = 16). Text xếp thành các dòng
ngắn (tối đa 5 ký tự/dòng) chạy xuống dần theo chiều dài màn — đúng dạng chuẩn Sofle.

## Mục tiêu

- Tạo cảm giác "một chiếc cyberdeck" khi bật máy: boot → SYSTEM ONLINE → UI.
- Màn trái luôn cho biết trạng thái layer/CapsLock/mode.
- Màn phải luôn có chuyển động (glitch cycler + progress bar).
- Idle tự tắt màn (tiết kiệm điện, chống burn-in) và đánh thức có hiệu ứng.
- Không làm bàn phím lag khi gõ (không dùng wait_ms blocking trong oled_task).

## Phi mục tiêu (non-goals)

- Không dùng WPM (người dùng không chọn).
- Không dùng sprite/bitmap animation (chọn hướng text glitch).
- Không thêm phím chỉnh độ sáng OLED (người dùng không chọn).
- Không đổi layout bàn phím; chỉ thêm OLED.

## Ngân sách bộ nhớ (đã đo thực tế)

| Build | Size | Free |
|-------|------|------|
| Hiện tại (chưa OLED) | 21,016 / 28,672 B (73%) | ~7.6 KB |
| OLED_ENABLE + render tối thiểu | 20,640 B (71%) | ~8 KB |

Ước tính toàn bộ thiết kế thêm ~1-2 KB (chuỗi text đặt PROGMEM) → tổng ~22-23 KB
(~80%), còn dư ~5 KB. An toàn với flash 28.7 KB của ATmega32U4.

## Kiến trúc & Setup

### rules.mk
```
TRI_LAYER_ENABLE = yes
ENCODER_MAP_ENABLE = yes
MOUSEKEY_ENABLE = yes
OLED_ENABLE = yes
OLED_DRIVER = ssd1306
```

### config.h
```
#define OLED_DISPLAY_128X32
#define SPLIT_OLED_ENABLE
```
- `SPLIT_OLED_ENABLE`: mỗi nửa tự chạy `oled_task_user` riêng cho OLED của nửa đó
  (nếu thiếu, chỉ master render, OLED nửa phải đứng yên).
- Rotation trong `oled_init_user()`: master = `OLED_ROTATION_270`, slave =
  `OLED_ROTATION_90` (2 nửa gương nhau → ngược 180°). Nếu thử thấy chữ lộn
  ngược thì hoán đổi thành 90/270. Xác nhận khi test phần cứng.

### Lưới hiển thị
- Font QMK mặc định 6x8; với rotation 90/270:
  - `oled_max_chars()` = 5 cột, `oled_max_lines()` = 16 dòng.
  - `oled_set_cursor(0, line)` với line 0..15; mỗi dòng tối đa 5 ký tự.
- Glyph khối `█`/`░` KHÔNG có trong font (bị logo QMK chiếm 0x80-0xD4) →
  progress bar dùng `#` (đầy) và `-` (trống).

### Cấu trúc code
- `oled_cyberdeck.c` + `oled_cyberdeck.h` (mới): toàn bộ logic OLED/animation.
  `keymap.c` chỉ include header và gọi `cyberdeck_key_pressed()` trong
  `process_record_user`. `rules.mk` thêm `SRC += oled_cyberdeck.c`.
- `oled_init_user()` — đặt rotation theo `is_keyboard_master()` (270/90).
- `oled_task_user()` — render chính, chia phase theo `timer_read32()`:
  1. BOOT
  2. MAIN
- PRNG tự viết (xorshift32) cho ký tự glitch/hex — không cần `RANDOM_ENABLE`.
- Biến phase lưu static, không phụ thuộc reset màn (dùng `timer_read32`).
- Tất cả chuỗi text đặt PROGMEM (`oled_write_P` / `PSTR`).

## Boot animation (cả 2 màn, ~4 giây)

```
CYBER   <- dòng 0
DECK    <- dòng 1
INITI   <- dòng 2
ATING   <- dòng 3
####-   <- dòng 4  (progress bar dọc: 3 dòng x 5 ô = 15 ô)
#####   <- dòng 5
-----   <- dòng 6
SYSTE   <- dòng 7  (SYSTEM ONLINE khi bar đầy)
M ONL
INE
```

1. **Glitch-in (0-1s)**: chữ hiện từng ký tự kiểu xáo loạn — mỗi ký tự nhấp nháy
   qua vài ký tự ngẫu nhiên trong bộ `{A-Z, 0-9, +, #, @, !}` rồi settle vào đúng
   chữ. Con trỏ `_` nhấp nháy cuối chữ INITIATING.
2. **Progress bar (0-3s)**: bar đầy **theo chiều dọc** — dòng 4: 5 ô, dòng 5: 5 ô,
   dòng 6: 5 ô (tổng 15 ô), mỗi ô `#` đầy / `-` trống, đầy dần theo thời gian.
3. **Scanline jitter**: cứ ~200ms nội dung nhích ngang 1 cột trong vài frame.
4. **SYSTEM ONLINE (3-4s)**: bar đầy → glitch nhẹ → hiện `SYSTEM ONLINE` (wrap
   5 chữ/dòng) ~0.5s.
5. Sang phase MAIN.

Boot chạy 1 lần duy nhất trong phiên (flag static), chỉ lặp lại khi reset bàn phím.

## Màn trái (master) — Thông tin (dạng dọc, 5 chữ/dòng)

```
L0  CYBER    <- header tĩnh (viền HUD góc phải)
L1  DECK
L2  (trống)
L3  LAYER    <- nhãn layer
L4  LOWER    <- tên layer (wrap ≤5 chữ/dòng, để trống dòng thừa)
L5  (trống)
L6  CAPS     <- nhãn caps
L7  ON       <- chỉ hiện khi bật, trống khi tắt
L8  MODE     <- nhãn mode
L9  WIN      <- WIN / MAC
```

- **L0-L1** — Header "CYBERDECK" cố định (wrap) + viền góc nhỏ kiểu HUD.
- **L3-L5** — Layer: tên layer wrap 5 chữ/dòng (QWERTY → `QWERT`/`Y`, COLEMAK →
  `COLEM`/`AK`, LOWER → `LOWER`, RAISE → `RAISE`, ADJUST → `ADJUS`/`T`). Đổi
  layer → glitch-out chữ cũ → glitch-in chữ mới (~300ms). Phát hiện đổi bằng cách
  so sánh `get_highest_layer(layer_state)` với giá trị static trước đó.
- **L6-L7** — Caps Lock: hiện `ON` khi bật (`host_keyboard_led_state()`), trống
  khi tắt. Lúc bật/tắt nhấp nháy 1 nhịp rồi ổn định.
- **L8-L9** — Mode: `WIN` / `MAC` (đọc `keymap_config.swap_lctl_lgui`, trạng thái
  CG_TOGG trên layer ADJUST). Đổi mode → glitch 1 nhịp.

Mọi ký tự render qua hàm glitch render chung (dùng chung PRNG).

## Màn phải (slave) — Glitch text cycler (dạng dọc)

**Danh sách câu (vòng lặp, PROGMEM):**
```
BRAINDANCE LOAD...
QUICKHACK
NETRUNNER
JACK IN
ICEBREAK
DAEMON UPLOAD
RELIC DETECTED
TRACE PROTOCOL
FLATLINE
SHARD COMPLETE
```

**Layout (5 chữ/dòng, text wrap xuống dưới):**
- L1-L8: câu text hiện tại, wrap 5 chữ/dòng (max 17 chữ → 4 dòng), căn giữa khối.
- L12-L14: progress bar dọc (3 dòng × 5 ô = 15 ô, `#`/`-`).
- L15: keystroke hex feedback (1 giá trị `0x7F` = 4 chữ, vừa 1 dòng; khi hex hoạt
  động thì tạm thay dòng 15 của bar).

**Chu kỳ mỗi câu (~3s):**
1. **Typing effect (~1s)**: ký tự xuất hiện từng chữ từ trái sang phải qua từng
   dòng wrap, con trỏ `_` nhấp nháy cuối chữ.
2. **Hold (~1.5s)**: câu đứng yên; thỉnh thoảng 1-2 ký tự ngẫu nhiên flicker
   (đổi thành ký tự rác 1 frame rồi về đúng chữ).
3. **Glitch-out (~0.3s)**: chữ vỡ thành ký tự rác dần rồi xóa → câu kế tiếp.

**Progress bar dọc:** đầy theo chu kỳ 3s rồi reset khi đổi câu. Dòng 15 bị hex
feedback tạm chiếm khi gõ phím.

**Keystroke hex feedback (dòng 15):**
- Mỗi lần gõ phím (theo dõi trong `process_record_user`), hiện 1 mã hex ngắn
  ngẫu nhiên (vd `0x7F`) tại dòng 15, flicker mờ dần ~1s rồi biến mất.
- Sinh hex bằng PRNG sẵn có, build chuỗi thủ công (không dùng snprintf để
  tiết kiệm flash).

**Ghi chú độ rộng:** max 5 ký tự/dòng (rotation 90/270, font 6x8 → 32px/6px=5).

**Scanline jitter**: mỗi ~1.5s nội dung nhích ngang 1 cột trong vài frame.

## Idle & đánh thức

- Không gõ ~2 phút → cả 2 màn glitch-out thành `SYSTEM OFFLINE` (~0.5s) →
  OLED tắt hẳn (`oled_off()`).
- Gõ phím bất kỳ → OLED bật lại (`oled_on()`), hiện `REBOOTING` glitch (~0.5s)
  → về UI chính.
- Đếm idle bằng `timer_read32()` (đồng hồ chung), reset khi có phím bấm
  (theo dõi trong `process_record_user` hoặc qua hook của OLED).

## Hiệu năng & xử lý lỗi

- Không dùng `wait_ms()`/`wait_cpuclock()` trong render — chỉ tính toán bằng
  `timer_read32()` và render frame mới nhất; tránh block vòng lặp chính.
- Dùng `oled_set_cursor` + `oled_write`/`oled_write_P` với buffer nhỏ trên stack
  (tối đa 6 bytes/dòng); không dùng buffer lớn.
- Tránh buffer lớn trên stack (RAM ATmega32U4 2.5 KB); chuỗi cố định đặt PROGMEM.
- Hai nửa hoạt động độc lập (SPLIT_OLED_ENABLE): nếu chỉ cắm 1 nửa vẫn chạy bình
  thường trên nửa đó.

## Testing

- `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap` — kiểm tra build &
  flash size (mục tiêu < 25 KB).
- Flash 1 bên thử nghiệm, quan sát:
  - Rotation: chữ đọc xuôi trái→phải trên cả 2 màn; nếu lộn ngược → hoán đổi
    270/90 trong `oled_init_user()`.
  - Boot: glitch-in → progress bar dọc → SYSTEM ONLINE → vào UI.
  - Đổi layer bằng phím LOWER/RAISE → màn trái glitch chữ layer.
  - Bật/tắt Caps Lock → indicator.
  - CG_TOGG trên ADJUST → đổi WIN/MAC.
  - Gõ phím liên tục → màn phải có hex feedback + không lag gõ.
  - Để yên 2 phút → SYSTEM OFFLINE → tắt; gõ → REBOOTING → về UI.
