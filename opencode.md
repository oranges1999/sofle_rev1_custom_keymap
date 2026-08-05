# Sofle Rev1 — Session: Full-screen Glitch + Tearing

Keyboard: `sofle/rev1` (split Pro Micro/ATmega32U4, 2x OLED I2C SSD1306 128x32).
Build: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap` — OK, 24146/28672 bytes (84%).

## Full-screen glitch + tearing (thay hiệu ứng glitch cũ)
- Task 1: gỡ `glitch_line_state` cũ; `oled_write_line_final` luôn ghi cột 0.
- Task 2: thêm `oled_apply_glitch()` — mỗi **2500ms** burst **300ms**, mỗi frame trong burst:
  - 4 band ngang dịch độc lập **0-6px** trái/phải -> đường xé giữa band
  - ~1/8 frame nhân đôi band (frame jump)
  - ~1/4 frame phủ noise bar dọc
- Final review: không Critical; finding "Important" (oled_clear bỏ đói smart-renderer) là dương tính giả — `oled_render()` chạy mỗi tick (`quantum/keyboard.c:750`) nên display vẫn full-refresh; Minor còn lại cosmetic, deferred.
- Size: 24146/28672 bytes (84%). **Đã flash cả 2 nửa, cả 2 màn hiển thị đúng mong đợi.**

## Hiển thị OLED màn trái — 3 dòng info đồng đều
- Helper `oled_write_info_line()` (`oled_cyberdeck.c:90`): label pad tới cột 10, value glitch-reveal.
- Format (value cùng cột 10):
  ```
  LAYER:    QWERTY
  CAPSLOCK: OFF
  MODE:     WIN
  ```
- `CAPSLOCK:` hiện cả OFF lẫn ON (trước chỉ ON khi bật); nhãn `CAPS` → `CAPSLOCK`.
- Size hiện tại: 24020/28672 bytes (83%). Đã flash, 2 màn hiển thị đúng.

## Bước cuối (Task 3 — người dùng flash)
```
qmk flash -kb sofle/rev1 -km sofle_rev1_custom_keymap
```
Flash cả 2 nửa. Kiểm tra: cứ ~2.5s có burst ~300ms — màn nhảy/xé band/dải nhiễu, cả 2 màn đều glitch, chữ vẫn đọc được giữa các burst, gõ liên tục không lag.
Tinh chỉnh cường độ: sửa `GLITCH_PERIOD_MS`/`GLITCH_BURST_MS`/`% 7` trong `oled_apply_glitch()` (`oled_cyberdeck.c`).

## Lưu ý
- KHÔNG tự `git commit` khi chưa được yêu cầu; đang làm trên nhánh `main`.
- Không dùng `wait_ms()`, chỉ `timer_read32()`; không sửa driver OLED.
- Các fix trước đó (chưa commit): bỏ `SPLIT_OLED_ENABLE`, bỏ idle-off (màn luôn bật), `phrases[][19]` (null terminator), `oled_clear()` mỗi frame MAIN.
