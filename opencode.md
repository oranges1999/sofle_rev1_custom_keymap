# Sofle Rev1 — Cyberdeck OLED

Bàn phím: `sofle/rev1` (split Pro Micro/ATmega32U4, 2x OLED I2C SSD1306 128x32).
Nửa trái là master, nửa phải là slave.

Build: `qmk compile -kb sofle/rev1 -km sofle_rev1_custom_keymap` — OK, 24390/28672 bytes (85%).
Nạp: `qmk flash -kb sofle/rev1 -km sofle_rev1_custom_keymap`, nạp cả 2 nửa.

Phiên bản hiển thị: v1.1.0 (`oled_cyberdeck.c`, biến `header` trong `render_left_main`).

## Boot — 5 giai đoạn tuần tự (cả 2 nửa)

| `t` (ms)    | Nội dung                                                             |
| ----------- | -------------------------------------------------------------------- |
| 0 – 900     | Dòng 0 gõ ra `CYBERDECK`, 100ms mỗi ký tự                            |
| 900 – 2000  | Dòng 1 gõ ra `INITIATING`, 110ms mỗi ký tự                           |
| 2000 – 3500 | Dòng 2 chạy thanh progress 0 → 21 (chia làm tròn lên để đạt đủ 21)   |
| 3500 – 3600 | Xoá cả 4 dòng                                                        |
| 3600 – 5000 | Dòng 1 nháy `SYSTEM ONLINE` bằng cách đảo `invert`, nửa chu kỳ 350ms |
| 5000        | Vào `PHASE_MAIN`, ghi `main_start`                                   |

Hằng số: `BOOT_T1_MS`, `BOOT_T2_MS`, `BOOT_T3_MS`, `BOOT_T4_MS`, `BOOT_TOTAL_MS`, `BOOT_BLINK_MS`.

Hai nửa có timer riêng nên chuỗi boot của chúng không đồng bộ tuyệt đối.

## Màn trái — load tuần tự rồi hiển thị trạng thái

Sau boot, 4 dòng hiện ra lần lượt theo `tm = now - main_start`: dòng 0 trong [0, 400), dòng 1 trong [400, 650), dòng 2 trong [650, 900), dòng 3 trong [900, 1150). Dòng chưa tới lượt thì để trống.

```
CYBERDECK // v1.1.0
LAYER:    QWERTY
CAPSLOCK: OFF
MODE:     WIN
```

Nhãn pad tới cột `INFO_LABEL_W` (10), value decode từng ký tự.

Hàm `info_reveal(tm, from, to, since_change)` gộp hai nguồn animation vào một quy tắc: trong cửa sổ load thì chạy theo `tm`; qua cửa sổ đó thì chạy theo `since_change` khi layer/caps/mode đổi; ngoài ra trả `REVEAL_DONE`. Trả `REVEAL_HIDDEN` nghĩa là dòng chưa tới lượt.

Hằng số: `MAIN_L0_MS`, `MAIN_L1_MS`, `MAIN_L2_MS`, `MAIN_L3_MS`.

## Màn phải — chạy theo nhịp gõ phím

State machine 4 trạng thái trong `render_right_main`:

| Trạng thái     | Thời lượng    | Dòng 0 | Dòng 1                        | Dòng 2     | Dòng 3                  |
| -------------- | ------------- | ------ | ----------------------------- | ---------- | ----------------------- |
| `SKILL_TYPE`   | tới khi gõ đủ | trống  | tên skill decode theo `typed` | trống      | trống                   |
| `SKILL_BAR`    | 1200ms        | trống  | tên đầy đủ                    | bar 0 → 21 | trống                   |
| `SKILL_LOADED` | 1200ms        | trống  | tên đầy đủ                    | bar đầy    | `[ LOADED ]` nháy 300ms |
| `SKILL_CLEAR`  | 250ms         | trống  | trống                         | trống      | trống                   |

`SKILL_CLEAR` xong thì `skill_idx` tiến, `typed` về 0, quay lại `SKILL_TYPE`.

Dòng 0 luôn trống là chủ ý: band trống tạo tương phản cho hiệu ứng glitch toàn màn.

`[ LOADED ]` nháy bằng cách bật/tắt hiển thị, khác với `SYSTEM ONLINE` ở boot dùng đảo `invert`.

Hằng số: `SKILL_BAR_MS`, `SKILL_LOADED_MS`, `SKILL_CLEAR_MS`, `LOADED_BLINK_MS`.

### Cách slave nhận được sự kiện phím

`process_record_user()` chỉ chạy trên master, nên nửa phải không thấy phím. Giải pháp là `SPLIT_ACTIVITY_ENABLE` trong `config.h`: QMK đồng bộ `last_matrix_activity_time()` từ master sang slave. Slave so timestamp với bản đã lưu, khác nghĩa là có hoạt động ma trận.

Hàm `skill_count_keys()` đếm parity và chỉ tăng `typed` ở mỗi lần đổi thứ hai, vì ma trận đổi cả lúc nhấn lẫn lúc nhả.

**Hạn chế đã biết — nhịp gõ hai tay không đều.** Giả định "một phím sinh đúng hai lần đổi ma trận" chỉ đúng với phím ở nửa master. Trên slave có nhiều nguồn ghi vào `last_matrix_modification_time`: chính slave quét được phím của mình, heartbeat `PUT_ACTIVITY` gửi lại mỗi 100ms kể cả dữ liệu không đổi, và giá trị của master đồng bộ sang. Nên phím ở nửa phải sinh 2-3 lần đổi thay vì 2, và `typed` tiến nhanh hơn. Thực tế: tên 18 ký tự cần khoảng 18 phím tay trái nhưng chỉ khoảng 7 phím tay phải.

Đây là lỗi trong mô hình của spec, không phải lỗi triển khai. Chức năng chính vẫn đúng: gõ ở cả hai nửa đều đẩy được animation. Nếu muốn tỉ lệ chính xác thì phải thay bằng split transaction riêng qua `SPLIT_TRANSACTION_IDS_USER` truyền một bộ đếm phím.

## Glitch toàn màn

`oled_apply_glitch()` chạy sau mỗi lần render ở `PHASE_MAIN`: cứ `GLITCH_PERIOD_MS` (2500ms) lại burst `GLITCH_BURST_MS` (300ms). Mỗi frame trong burst:

- 4 band ngang dịch độc lập 0-6px trái/phải, tạo đường xé giữa các band
- ~1/8 frame nhân đôi band (frame jump)
- ~1/10 band bị dropout hoàn toàn — tạo tương phản để frame jump nổi rõ cả trên màn có 4 dòng text đều nhau
- ~1/4 frame phủ noise bar dọc

Tinh chỉnh cường độ: sửa `GLITCH_PERIOD_MS`, `GLITCH_BURST_MS`, `% 7`, `% 10` trong `oled_apply_glitch()`.

## Test host

`./tests/run.sh` biên dịch thẳng `oled_cyberdeck.c` bằng `gcc` với bộ stub QMK trong `tests/quantum.h`, rồi chạy assert trong `tests/test_line.c`. Không framework, không phụ thuộc phần cứng.

Chạy được vì `rules.mk` chỉ khai báo `SRC += oled_cyberdeck.c` nên QMK không đụng tới `tests/`.

**Giới hạn quan trọng:** stub định nghĩa `pgm_read_byte` thành phép dereference thường, nên bộ test **không phát hiện được việc quên `pgm_read_byte`**. Code đọc thẳng con trỏ PROGMEM sẽ pass trên host và trả về rác trên AVR. Phải tự soát bằng mắt khi thêm code đọc chuỗi.

Các assert đã được kiểm bằng mutation testing: xoá con trỏ decode, đổi text `[ LOADED ]`, hoán `#`/`-`, đảo pha nháy, render sai `skill_idx` — tất cả đều làm test fail.

## Lưu ý

- KHÔNG tự `git commit` khi chưa được yêu cầu.
- Không dùng `wait_ms()` hay bất kỳ hàm chặn nào trong đường render, chỉ `timer_read32()` và so mốc. Chặn đường render sẽ làm trễ quét phím.
- Không sửa driver OLED trong `drivers/oled/`.
- Chuỗi hằng phải nằm trong PROGMEM và đọc bằng `pgm_read_byte()`.
- `LINE_COLS` là 21. Mọi dòng ghi ra OLED phải đúng 21 ký tự cộng `'\0'` ở `buf[21]`.
- `OLED_TIMEOUT 0` trong `config.h` để hai màn luôn bật. Không có nó thì mặc định 60s, và màn phải sẽ tắt nếu chỉ gõ ở nửa trái — vì `SPLIT_ACTIVITY_ENABLE` đồng bộ timestamp nhưng không gọi `oled_on()`.
- Chuỗi `header` hiện dài đúng 19 ký tự, khớp với hằng `19` hardcode trong lời gọi `oled_write_line_type` ở `render_left_main`. Nếu bump version làm đổi độ dài (ví dụ `v1.10.0`), phải sửa hằng đó theo.

## Cần kiểm khi nạp

1. Boot ~5s, các giai đoạn nối tiếp chứ không chồng nhau.
2. Thanh bar boot chạy đủ 21/21 rồi mới xoá màn.
3. Màn trái đổ ra 4 dòng lần lượt trong ~1.15s.
4. **Đếm số phím cần để gõ hết một tên skill, thử riêng tay trái rồi riêng tay phải** — xem phần hạn chế ở trên.
5. Gõ hết tên thì bar tự chạy, xong nháy `[ LOADED ]`, rồi xoá màn chờ skill kế.
6. Ngừng gõ giữa chừng thì màn phải đứng yên, không tự chạy tiếp.
7. Nhịp `[ LOADED ]` (300ms) và chu kỳ glitch (2500ms) đủ gần để burst có thể rơi trúng cùng pha nháy, nhìn ra như màn đơ chứ không phải hiệu ứng.
8. Gõ liên tục không trễ phím.
