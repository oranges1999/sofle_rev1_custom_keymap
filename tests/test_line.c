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
    assert(stub_screen[0][0] != ' '); // ký tự rác đang giải mã, ngay cột 0
    assert(stub_screen[0][1] == ' ');
    assert(strlen(stub_screen[0]) == 21);

    // != ' ' là chưa đủ: ký tự thật ('A') cũng != ' '. Lấy mẫu nhiều lần để
    // chứng minh vị trí con trỏ thật sự là rác, không phải ký tự thật của chuỗi.
    bool saw_non_real = false;
    for (int i = 0; i < 8; i++) {
        oled_write_line_type(PSTR("ABC"), 0, 0, false);
        if (stub_screen[0][0] != 'A') saw_non_real = true;
    }
    assert(saw_non_real);
}

static void test_type_reveal_giua(void) {
    oled_write_line_type(PSTR("ABCDE"), 1, 2, false);
    assert(strncmp(stub_screen[1], "AB", 2) == 0);
    assert(stub_screen[1][2] != ' '); // rác tại vị trí 2 của chuỗi
    assert(stub_screen[1][3] == ' ');
    assert(strlen(stub_screen[1]) == 21);

    // Ký tự tại con trỏ phải là rác, không phải ký tự thật của chuỗi.
    bool saw_non_real = false;
    for (int i = 0; i < 8; i++) {
        oled_write_line_type(PSTR("ABCDE"), 1, 2, false);
        if (stub_screen[1][2] != 'C') saw_non_real = true;
    }
    assert(saw_non_real);
}

static void test_type_reveal_bang_do_dai(void) {
    oled_write_line_type(PSTR("ABCDE"), 2, 5, false);
    expect_line(2, "ABCDE");
}

static void test_type_reveal_vuot_do_dai(void) {
    oled_write_line_type(PSTR("ABCDE"), 3, 99, false);
    expect_line(3, "ABCDE");
}

static void test_type_chuoi_dai_bi_cat(void) {
    oled_write_line_type(PSTR("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), 0, 99, false);
    expect_line(0, "ABCDEFGHIJKLMNOPQRSTU");
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

    // Ký tự tại con trỏ phải là rác, không phải ký tự thật 'I' của "WIN".
    bool saw_non_real = false;
    for (int i = 0; i < 8; i++) {
        oled_write_info_line(2, PSTR("MODE:"), PSTR("WIN"), 1, false);
        if (stub_screen[2][11] != 'I') saw_non_real = true;
    }
    assert(saw_non_real);
}

static void test_info_reveal(void) {
    // Chưa tới lượt dòng này thì báo ẩn.
    assert(info_reveal(0, 400, 650, 999999) == REVEAL_HIDDEN);
    assert(info_reveal(399, 400, 650, 999999) == REVEAL_HIDDEN);

    // Trong cửa sổ load thì reveal chạy từ 0 lên.
    assert(info_reveal(400, 400, 650, 999999) == 0);
    assert(info_reveal(649, 400, 650, 999999) == 7);

    // Qua cửa sổ load, giá trị vừa đổi thì chạy animation đổi giá trị.
    assert(info_reveal(5000, 400, 650, 0) == 0);
    assert(info_reveal(5000, 400, 650, 200) == 5);

    // Qua cửa sổ load, giá trị không đổi lâu rồi thì hiện đầy.
    assert(info_reveal(5000, 400, 650, 300) == REVEAL_DONE);
    assert(info_reveal(5000, 400, 650, 999999) == REVEAL_DONE);
}

// Màn trái: dòng 0 không còn tiền tố "> ", và caps lock bật thì đảo màu cả dòng.
static void test_man_trai_header_va_caps(void) {
    stub_timer                   = 700000;
    main_start                   = stub_timer - 5000; // qua hẳn cửa sổ load
    layer_state                  = 0;
    keymap_config.swap_lctl_lgui = false;

    stub_led.caps_lock = false;
    render_left_main(stub_timer);

    // Header bắt đầu ngay cột 0, không có dấu nhắc.
    expect_line(0, "CYBERDECK // v1.2.1");
    assert(strncmp(stub_screen[2], "CAPSLOCK: OFF", 13) == 0);
    assert(!stub_invert[2]);

    // Bật caps: đổi giá trị nên chạy animation decode, cho qua hẳn rồi mới kiểm.
    stub_led.caps_lock = true;
    render_left_main(stub_timer);
    stub_timer += 400;
    render_left_main(stub_timer);

    assert(strncmp(stub_screen[2], "CAPSLOCK: ON", 12) == 0);
    assert(stub_invert[2]); // cả dòng đảo màu

    // Không đảo lây sang dòng khác.
    assert(!stub_invert[0]);
    assert(!stub_invert[1]);
    assert(!stub_invert[3]);

    // Tắt caps thì hết đảo màu.
    stub_led.caps_lock = false;
    render_left_main(stub_timer);
    stub_timer += 400;
    render_left_main(stub_timer);
    assert(!stub_invert[2]);
}

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

    // Ngay khi vừa vào SKILL_LOADED: bar (dòng 1) phải đầy, câu báo (dòng 2)
    // đang ở nửa nhấp nháy bật — kiểm nội dung thật sự vẽ ra màn, không chỉ
    // suy từ state machine.
    for (uint8_t i = 0; i < LINE_COLS; i++) {
        assert(stub_screen[1][i] == '#'); // bar đầy
    }
    expect_line(2, "   [ SUBNET OPEN ]"); // câu báo ghép cặp với skills[0]

    // Qua nửa chu kỳ nhấp nháy kế tiếp: câu báo phải tắt hẳn.
    stub_timer += LOADED_BLINK_MS;
    render_right_main(stub_timer);
    expect_line(2, "");

    stub_timer += SKILL_LOADED_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_CLEAR);

    stub_timer += SKILL_CLEAR_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_TYPE);
    assert(skill_idx == 1);
    assert(typed == 0);

    // Dòng 0 phải vẽ skills[skill_idx] (skill thứ 2), không phải skills[0]
    // còn sót lại từ vòng trước.
    press_key();
    assert(typed == 1);
    assert(stub_screen[0][CMD_PREFIX_W] == 'I'); // ký tự đầu của "ICEPICK UPLOAD"
}

static void test_done_msg_dung_dinh_dang(void) {
    oled_write_done_line(0, done_msgs[0]);
    expect_line(0, "   [ SUBNET OPEN ]");
    oled_write_done_line(1, done_msgs[2]);
    expect_line(1, "   [ PACKAGE PULLED ]");
    oled_write_done_line(2, done_msgs[9]);
    expect_line(2, "   [ FLATLINE ]");
}

// Chạy trọn một chu kỳ của skill thứ `idx` rồi trả về ở trạng thái SKILL_LOADED,
// nửa nhấp nháy đang bật.
static void run_cycle_to_loaded(void) {
    uint8_t len = vstrlen(skills[skill_idx]);
    while (typed < len) {
        press_key();
    }
    assert(skill_st == SKILL_BAR);
    stub_timer += SKILL_BAR_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_LOADED);
}

// Câu báo phải ghép theo skill_idx đang chạy. Kiểm ở chỉ số khác 0, nếu không
// thì mutation `done_msgs[skill_idx]` -> `done_msgs[0]` sẽ sống sót.
static void test_done_msg_ghep_theo_skill_idx(void) {
    stub_timer = 600000;
    reset_skill_state();

    run_cycle_to_loaded();
    expect_line(2, "   [ SUBNET OPEN ]"); // skills[0]

    // Sang skill kế.
    stub_timer += SKILL_LOADED_MS;
    render_right_main(stub_timer);
    stub_timer += SKILL_CLEAR_MS;
    render_right_main(stub_timer);
    assert(skill_idx == 1);

    run_cycle_to_loaded();
    expect_line(2, "   [ ICE SHATTERED ]"); // skills[1], khác hẳn phần tử 0

    // Và một chỉ số nữa cho chắc.
    stub_timer += SKILL_LOADED_MS;
    render_right_main(stub_timer);
    stub_timer += SKILL_CLEAR_MS;
    render_right_main(stub_timer);
    assert(skill_idx == 2);

    run_cycle_to_loaded();
    expect_line(2, "   [ PACKAGE PULLED ]"); // skills[2]
}

// Mọi câu báo phải vừa 21 cột, và mọi tên lệnh phải vừa sau tiền tố "> ".
static void test_moi_chuoi_vua_do_rong(void) {
    for (uint8_t i = 0; i < NUM_SKILLS; i++) {
        assert(vstrlen(done_msgs[i]) <= DONE_MSG_W);
        assert(vstrlen(skills[i]) <= LINE_COLS - CMD_PREFIX_W);
    }
}

static void test_cursor_khoi_nhap_nhay(void) {
    // Con trỏ bật: ô ngay sau ký tự cuối là space đảo màu (khối đặc).
    oled_write_line_cursor(PSTR("ABC"), 0, 2, true);
    assert(strncmp(stub_screen[0], "> AB", 4) == 0);
    assert(stub_screen[0][4] == ' ');
    assert(stub_inv_cell[0][4]);      // chính là khối con trỏ
    assert(!stub_inv_cell[0][3]);     // ký tự thật không bị đảo màu
    assert(strlen(stub_screen[0]) == 21);

    // Con trỏ tắt: cùng vị trí đó không còn đảo màu.
    oled_write_line_cursor(PSTR("ABC"), 1, 2, false);
    assert(strncmp(stub_screen[1], "> AB", 4) == 0);
    assert(!stub_inv_cell[1][4]);

    // Text hiện thẳng, không decode: gõ hết thì không còn ký tự rác nào.
    oled_write_line_cursor(PSTR("ABC"), 2, 3, false);
    expect_line(2, "> ABC");

    // shown vượt độ dài thì kẹp lại, con trỏ không trôi ra ngoài chuỗi.
    oled_write_line_cursor(PSTR("ABC"), 3, 99, true);
    expect_line(3, "> ABC");
    assert(stub_inv_cell[3][CMD_PREFIX_W + 3]);
}

// Trong SKILL_TYPE con trỏ phải nhấp nháy; sang SKILL_BAR thì tắt hẳn.
static void test_cursor_chi_nhap_nhay_luc_go(void) {
    stub_timer = 500000 - (500000 % (CURSOR_BLINK_MS * 2)); // rơi vào nửa bật
    reset_skill_state();
    render_right_main(stub_timer);
    assert(skill_st == SKILL_TYPE);
    assert(stub_inv_cell[0][CMD_PREFIX_W + typed]);

    stub_timer += CURSOR_BLINK_MS; // nửa tắt
    render_right_main(stub_timer);
    assert(!stub_inv_cell[0][CMD_PREFIX_W + typed]);

    // Gõ hết tên rồi thì dù đang ở nửa "bật" con trỏ vẫn không được vẽ.
    uint8_t len = vstrlen(skills[skill_idx]);
    while (skill_st == SKILL_TYPE) {
        press_key();
        if (typed > len + 2) break; // chốt chặn, không để lặp vô hạn
    }
    assert(skill_st == SKILL_BAR);
    stub_timer -= (stub_timer % (CURSOR_BLINK_MS * 2)); // về nửa bật
    render_right_main(stub_timer);
    for (uint8_t i = 0; i < LINE_COLS; i++) {
        assert(!stub_inv_cell[0][i]);
    }
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

// Dòng 3 giờ là băng trống giữ tương phản cho glitch (trước đây là dòng 0).
static void test_skill_dong_3_luon_trong(void) {
    stub_timer = 400000;
    reset_skill_state();

    render_right_main(stub_timer); // SKILL_TYPE
    expect_line(3, "");

    uint8_t len = vstrlen(skills[skill_idx]);
    for (uint8_t i = 0; i < len; i++) {
        press_key();
    }
    assert(skill_st == SKILL_BAR);
    render_right_main(stub_timer);
    expect_line(3, ""); // SKILL_BAR

    stub_timer += SKILL_BAR_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_LOADED);
    expect_line(3, ""); // SKILL_LOADED

    // SKILL_CLEAR phải xoá sạch cả 4 dòng, không sót dòng nào từ vòng trước.
    stub_timer += SKILL_LOADED_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_CLEAR);
    for (uint8_t line = 0; line < 4; line++) {
        expect_line(line, "");
    }
}

int main(void) {
    test_type_reveal_zero();
    test_type_reveal_giua();
    test_type_reveal_bang_do_dai();
    test_type_reveal_vuot_do_dai();
    test_type_chuoi_dai_bi_cat();
    test_info_line_day_du();
    test_info_line_dang_decode();
    test_info_reveal();
    test_man_trai_header_va_caps();
    test_skill_go_du_ten_thi_chay_bar();
    test_skill_chu_ky_day_du();
    test_done_msg_dung_dinh_dang();
    test_done_msg_ghep_theo_skill_idx();
    test_moi_chuoi_vua_do_rong();
    test_cursor_khoi_nhap_nhay();
    test_cursor_chi_nhap_nhay_luc_go();
    test_skill_ngung_go_thi_dung_yen();
    test_skill_dong_3_luon_trong();
    printf("tất cả test qua\n");
    return 0;
}
