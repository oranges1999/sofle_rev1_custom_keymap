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

    // != ' ' là chưa đủ: ký tự thật ('A') cũng != ' '. Lấy mẫu nhiều lần để
    // chứng minh vị trí con trỏ thật sự là rác, không phải ký tự thật của chuỗi.
    bool saw_non_real = false;
    for (int i = 0; i < 8; i++) {
        oled_write_line_type(PSTR("ABC"), 0, 0, false);
        if (stub_screen[0][2] != 'A') saw_non_real = true;
    }
    assert(saw_non_real);
}

static void test_type_reveal_giua(void) {
    oled_write_line_type(PSTR("ABCDE"), 1, 2, false);
    assert(strncmp(stub_screen[1], "> AB", 4) == 0);
    assert(stub_screen[1][4] != ' '); // rác tại vị trí 2 của chuỗi
    assert(stub_screen[1][5] == ' ');
    assert(strlen(stub_screen[1]) == 21);

    // Ký tự tại con trỏ phải là rác, không phải ký tự thật của chuỗi.
    bool saw_non_real = false;
    for (int i = 0; i < 8; i++) {
        oled_write_line_type(PSTR("ABCDE"), 1, 2, false);
        if (stub_screen[1][4] != 'C') saw_non_real = true;
    }
    assert(saw_non_real);
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

    // Ngay khi vừa vào SKILL_LOADED: bar phải đầy, chữ LOADED đang ở nửa nhấp
    // nháy bật (thật sự vẽ ra màn, không chỉ suy từ state machine).
    for (uint8_t i = 0; i < LINE_COLS; i++) {
        assert(stub_screen[2][i] == '#'); // bar đầy
    }
    assert(strncmp(stub_screen[3], "   [ LOADED ]", 13) == 0); // nhấp nháy đang bật

    // Qua nửa chu kỳ nhấp nháy kế tiếp: chữ LOADED phải tắt hẳn.
    stub_timer += LOADED_BLINK_MS;
    render_right_main(stub_timer);
    assert(strncmp(stub_screen[3], "   [ LOADED ]", 13) != 0);

    stub_timer += SKILL_LOADED_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_CLEAR);

    stub_timer += SKILL_CLEAR_MS;
    render_right_main(stub_timer);
    assert(skill_st == SKILL_TYPE);
    assert(skill_idx == 1);
    assert(typed == 0);

    // Dòng 1 phải vẽ skills[skill_idx] (skill thứ 2, "QUICKHACK"), không phải
    // skills[0] ("BRAINDANCE LOAD...") còn sót lại từ vòng trước.
    press_key();
    assert(typed == 1);
    assert(stub_screen[1][2] == 'Q'); // ký tự thật đầu tiên của skills[1]
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

int main(void) {
    test_type_reveal_zero();
    test_type_reveal_giua();
    test_type_reveal_bang_do_dai();
    test_type_reveal_vuot_do_dai();
    test_type_chuoi_dai_bi_cat();
    test_info_line_day_du();
    test_info_line_dang_decode();
    test_info_reveal();
    test_skill_go_du_ten_thi_chay_bar();
    test_skill_chu_ky_day_du();
    test_skill_ngung_go_thi_dung_yen();
    test_skill_dong_0_luon_trong();
    printf("tất cả test qua\n");
    return 0;
}
