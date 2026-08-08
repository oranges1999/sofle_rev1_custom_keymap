# Thiết kế: Boot tuần tự + màn phải chạy theo nhịp gõ

Ngày: 2026-08-08
Phần cứng: Sofle rev1 (Pro Micro ATmega32U4, 2 OLED SSD1306 128x32, I2C)
File chịu ảnh hưởng: `oled_cyberdeck.c`, `oled_cyberdeck.h`, `keymap.c`, `config.h`

## Mục tiêu

1. Đổi màn boot (cả hai nửa) từ kiểu reveal song song sang một chuỗi tuần tự: gõ chữ ra từng ký tự, xong chữ mới chạy thanh progress, progress đầy thì xoá màn và nhấp nháy `SYSTEM ONLINE`, sau đó mới load màn chính theo từng dòng.
2. Đổi màn phải từ vòng lặp phrase theo thời gian sang vòng lặp điều khiển bởi nhịp gõ phím: mỗi phím gõ đẩy tên skill ra thêm một ký tự, tên đầy thì thanh progress tự chạy, chạy xong thì nhấp nháy dòng báo hoàn tất bên dưới thanh progress, rồi xoá màn để sang chu kỳ mới.

## Bối cảnh: sự kiện phím không tới được màn phải

`cyberdeck_key_pressed()` được gọi từ `process_record_user()` trong `keymap.c`. QMK chỉ chạy `process_record_user()` trên nửa master; nửa slave chỉ quét ma trận rồi gửi trạng thái sang master. Màn phải nằm trên slave, nên biến `hex_start`/`hex_val` mà `render_right_main()` đang đọc không bao giờ được ghi. Hiệu ứng hex feedback ở màn phải trên thực tế chưa từng chạy.

Vì yêu cầu mới đặt toàn bộ nhịp của màn phải lên sự kiện phím, phải có cơ chế đưa sự kiện đó sang slave.

### Phương án đã chọn: `SPLIT_ACTIVITY_ENABLE`

Thêm `#define SPLIT_ACTIVITY_ENABLE` vào `config.h`. QMK sẽ đồng bộ `last_matrix_activity_time()` (cùng timestamp encoder và pointing device) từ master sang slave qua split transaction có sẵn (`quantum/split_common/transactions.c`). Slave đọc `last_matrix_activity_time()` và so với giá trị đã lưu; khác nghĩa là có hoạt động ma trận mới.

Đánh đổi: nếu gõ rất nhanh, nhiều sự kiện có thể bị gộp thành một lần đồng bộ, nên số ký tự hiện ra có thể ít hơn số phím đã gõ. Chấp nhận được vì yêu cầu chỉ cần cảm giác "gõ ra chữ", không cần khớp từng phím.

Phương án thay thế đã cân nhắc và loại: đăng ký split transaction riêng qua `SPLIT_TRANSACTION_IDS_USER` để truyền một bộ đếm phím. Chính xác tuyệt đối nhưng tốn khoảng 30 dòng và không mang lại khác biệt thấy được. Giữ làm đường nâng cấp nếu chạy thật thấy hụt ký tự rõ rệt.

## Kiểu hiển thị chữ: decode từng ký tự

Thay hành vi của tham số `reveal`. Hàm `oled_write_line_full()` đổi tên thành `oled_write_line_type()` với quy tắc mới cho từng vị trí `i` trong chuỗi:

- `i < reveal`: ghi ký tự thật
- `i == reveal`: ghi `glitch_char()`, đổi mỗi frame
- `i > reveal`: ghi khoảng trắng

Nghĩa là phần chưa tới hoàn toàn trống, chỉ có đúng một ký tự đang "giải mã" ở đầu con trỏ. Khác với hành vi cũ là loạn toàn bộ phần chưa reveal.

Tham số `cursor` của hàm cũ bị bỏ. Ký tự rác ở vị trí `reveal` đã đóng vai trò con trỏ, không cần dấu `_` riêng. Kể cả khi `reveal = 0` (chưa gõ ký tự nào) thì vị trí 0 vẫn là ký tự rác nhấp nháy, đủ để báo màn đang chờ.

Tiền tố `"> "` ở đầu dòng giữ nguyên như hàm cũ, nên chuỗi nội dung tối đa vẫn là `LINE_COLS - 2` = 19 ký tự.

`oled_write_info_line()` áp dụng đúng quy tắc trên cho phần value.

`oled_write_line_flicker()` bị xoá vì không còn nơi dùng.

## Màn boot

Toàn bộ nằm trong `render_boot()`, phân nhánh theo `t = now - boot_start`. Không thêm state mới vì chuỗi này thuần tuyến tính theo thời gian.

| `t` (ms)    | Nội dung hiển thị                                                                                                   |
| ----------- | ------------------------------------------------------------------------------------------------------------------- |
| 0 – 900     | Dòng 0 decode `CYBERDECK`, 100ms mỗi ký tự. Dòng 1, 2, 3 trống.                                                     |
| 900 – 2000  | Dòng 0 hiện đủ. Dòng 1 decode `INITIATING`, 110ms mỗi ký tự. Dòng 2, 3 trống.                                       |
| 2000 – 3500 | Hai dòng chữ đứng yên. Dòng 2 chạy progress bar từ 0 đến 21 đơn vị. Dòng 3 trống.                                   |
| 3500 – 3600 | Xoá cả 4 dòng.                                                                                                      |
| 3600 – 5000 | Dòng 1 hiện `SYSTEM ONLINE` và nhấp nháy bằng cách đảo `invert`, chu kỳ 350ms (khoảng 4 nhịp). Các dòng khác trống. |
| 5000        | Xoá màn, chuyển `PHASE_MAIN`, ghi `main_start = now`.                                                               |

`BOOT_TOTAL_MS` đổi từ 4000 thành 5000. Các mốc còn lại khai báo thành hằng số riêng để chỉnh không phải đếm tay.

## Màn trái sau boot: load tuần tự từng dòng

Theo `tm = now - main_start`, mỗi dòng có cửa sổ decode riêng, kế tiếp nhau:

| `tm` (ms)  | Dòng                                     |
| ---------- | ---------------------------------------- |
| 0 – 400    | Dòng 0: `CYBERDECK // v1.0.1` (19 ký tự) |
| 400 – 650  | Dòng 1: `LAYER:` + tên layer             |
| 650 – 900  | Dòng 2: `CAPSLOCK:` + `ON`/`OFF`         |
| 900 – 1150 | Dòng 3: `MODE:` + `WIN`/`MAC`            |

Dòng chưa tới cửa sổ của mình thì để trống. Sau `tm > 1150` màn trái về trạng thái thường.

Hiệu ứng decode sẵn có khi đổi layer, caps lock hay mode giữ nguyên không sửa. Ba biến `layer_anim`, `caps_anim`, `mode_anim` khởi tạo bằng `main_start` để lần load đầu không bị kích hoạt trùng với animation đổi giá trị.

## Màn phải: state machine theo nhịp gõ

```c
enum skill_state { SKILL_TYPE, SKILL_BAR, SKILL_LOADED, SKILL_CLEAR };
```

Trạng thái hiện tại giữ trong `skill_state`, mốc thời gian vào trạng thái giữ trong `state_start`, số ký tự đã hiện giữ trong `typed`, chỉ số skill đang chạy giữ trong `skill_idx`.

Bố cục 4 dòng theo từng trạng thái:

| Trạng thái     | Thời lượng    | Dòng 0 | Dòng 1                                                       | Dòng 2            | Dòng 3                              |
| -------------- | ------------- | ------ | ------------------------------------------------------------ | ----------------- | ----------------------------------- |
| `SKILL_TYPE`   | tới khi gõ đủ | trống  | `> QUICKHAC▒` (decode theo `typed`; `typed = 0` thành `> ▒`) | trống             | trống                               |
| `SKILL_BAR`    | 1200ms        | trống  | `> QUICKHACK`                                                | progress bar 0→21 | trống                               |
| `SKILL_LOADED` | 1200ms        | trống  | `> QUICKHACK`                                                | progress bar đầy  | `[ LOADED ]` nhấp nháy chu kỳ 300ms |
| `SKILL_CLEAR`  | 250ms         | trống  | trống                                                        | trống             | trống                               |

Chuyển trạng thái:

- `SKILL_TYPE` → `SKILL_BAR` khi `typed >= vstrlen(skills[skill_idx])`
- `SKILL_BAR` → `SKILL_LOADED` khi hết 1200ms
- `SKILL_LOADED` → `SKILL_CLEAR` khi hết 1200ms
- `SKILL_CLEAR` → `SKILL_TYPE` khi hết 250ms, đồng thời `skill_idx = (skill_idx + 1) % NUM_SKILLS` và `typed = 0`

Dòng 0 luôn trống. Đây là chủ ý: band trống tạo tương phản để hiệu ứng frame jump và noise bar của `oled_apply_glitch()` nổi rõ.

Dòng 2 dùng `oled_write_status_line()` giữ nguyên, không có tiền tố `"> "`.

Dòng 3 ở `SKILL_LOADED` ghi chuỗi `[ LOADED ]` bắt đầu từ cột 3, phần còn lại của dòng là khoảng trắng. Nhấp nháy bằng cách bật tắt hiển thị: nửa chu kỳ đầu ghi chuỗi, nửa chu kỳ sau ghi dòng trống. Chu kỳ 300ms nên trong 1200ms có 4 nhịp. Cách này khác với `SYSTEM ONLINE` ở màn boot (đảo `invert`) là chủ ý: `SYSTEM ONLINE` cần nổi khối trắng, còn `[ LOADED ]` cần cảm giác tín hiệu nhấp nháy.

Khi vừa vào `PHASE_MAIN` mà chưa gõ phím nào, `typed = 0` nên dòng 1 hiện `>` cùng một ký tự rác nhấp nháy ở vị trí đầu. Màn phải không có bước load tuần tự riêng vì nội dung của nó vốn sinh ra từ thao tác gõ.

### Đếm phím

Trong nhánh `SKILL_TYPE`, so `last_matrix_activity_time()` với giá trị đã lưu. Khi khác nhau thì ghi nhận một sự kiện ma trận. Ma trận đổi cả lúc nhấn lẫn lúc nhả, nên một phím sinh hai sự kiện; chỉ tăng `typed` ở sự kiện thứ hai của mỗi cặp để đạt tỉ lệ một phím một ký tự.

Nếu đồng bộ bỏ lỡ sự kiện thì parity có thể lệch một nhịp. Không xử lý riêng: tỉ lệ trung bình vẫn xấp xỉ một ký tự mỗi phím, và đây là hiệu ứng thẩm mỹ.

Chỉ đếm ở trạng thái `SKILL_TYPE`. Phím gõ trong `SKILL_BAR`, `SKILL_LOADED`, `SKILL_CLEAR` bị bỏ qua.

## Phần bị xoá

- `hex_start`, `hex_val`, `HEX_ANIM_MS`, `cyberdeck_key_pressed()` và khai báo trong `oled_cyberdeck.h`
- Lời gọi `cyberdeck_key_pressed()` trong `process_record_user()` ở `keymap.c`
- `oled_write_line_flicker()`
- `CYCLE_MS`, `TYPE_MS`, `GLITCHOUT_T` và toàn bộ vòng lặp phrase theo thời gian trong `render_right_main()`

Nếu sau khi xoá `cyberdeck_key_pressed()` mà `process_record_user()` không còn việc gì ngoài `switch` xử lý `KC_PRVWD`/`KC_NXTWD`/`KC_LSTRT`/`KC_LEND` thì giữ nguyên phần `switch`, chỉ bỏ khối `if (record->event.pressed)` ở đầu.

## Phần giữ nguyên

- `oled_apply_glitch()` cùng band dropout, không sửa
- Nội dung 4 dòng của màn trái
- Mảng 10 phrase, đổi tên biến từ `phrases` sang `skills` và `NUM_PHRASES` sang `NUM_SKILLS`
- `oled_init_user()`, hướng xoay hai màn
- PRNG và `glitch_char()`

## Kiểm thử

Firmware AVR không chạy được test harness tại chỗ. Bổ sung một file kiểm tra biên dịch trên host:

`test_line.c` — biên dịch bằng `gcc` trên máy phát triển, stub `pgm_read_byte()` thành truy cập mảng thường và stub `oled_set_cursor()`/`oled_write()` để bắt lấy buffer. Assert nội dung buffer do `oled_write_line_type()` sinh ra tại các mốc:

- `reveal = 0`: không có ký tự thật, vị trí 0 là ký tự rác, phần còn lại là khoảng trắng
- `reveal` ở giữa chuỗi: đúng `reveal` ký tự thật, một ký tự rác kế tiếp, sau đó toàn khoảng trắng
- `reveal = độ dài chuỗi`: toàn bộ ký tự thật, không còn ký tự rác
- `reveal` lớn hơn độ dài chuỗi: không tràn buffer, không sinh ký tự rác
- Chuỗi dài hơn `LINE_COLS - 2`: cắt đúng, `buf[LINE_COLS]` vẫn là `'\0'`

Đây là chỗ dễ sai lệch chỉ số nhất. Phần còn lại (chuyển trạng thái, mốc thời gian, cảm giác thị giác) kiểm bằng biên dịch và nạp firmware thật.

Tiêu chí nghiệm thu trên phần cứng:

1. Cắm phím: cả hai màn chạy đúng chuỗi boot 5 giây, các bước tuần tự chứ không chồng lên nhau.
2. Vào màn chính: màn trái đổ ra bốn dòng lần lượt từ trên xuống.
3. Gõ phím: màn phải hiện dần tên skill, khoảng một ký tự mỗi phím.
4. Gõ hết tên: thanh progress tự chạy, xong thì `[ LOADED ]` nhấp nháy dưới thanh, rồi màn xoá và chờ gõ tiếp cho skill kế.
5. Ngừng gõ giữa chừng: màn phải đứng tại tên skill dở, không tự chạy tiếp.
6. Glitch định kỳ vẫn chạy trên cả hai màn, gõ liên tục không thấy trễ.

## Ngân sách flash

Trước thay đổi: 24072/28672 byte (83%), còn trống 4600 byte.

Phần xoá (hex feedback, flicker, vòng lặp phrase) bù cho phần thêm (state machine, các mốc boot). `SPLIT_ACTIVITY_ENABLE` thêm khoảng 150 byte. Dự kiến kết quả vẫn quanh 83–84%. Nếu vượt 95% thì dừng lại xem xét trước khi nạp.

## Điểm cần theo dõi khi triển khai

- Hai nửa có timer riêng, chuỗi boot của chúng không đồng bộ tuyệt đối. Đây là hành vi sẵn có, không xử lý.
- `oled_clear()` đang được gọi mỗi frame ở `PHASE_MAIN`. Chuỗi boot mới cũng cần các dòng trống thật sự; ghi dòng toàn khoảng trắng là đủ, không cần gọi thêm `oled_clear()`.
- `last_matrix_activity_time()` trả về timestamp theo timer của master sau khi đồng bộ. Chỉ dùng để so sánh khác/giống, không dùng giá trị tuyệt đối.
