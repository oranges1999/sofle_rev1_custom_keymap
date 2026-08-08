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

int main(void) {
    test_type_reveal_zero();
    test_type_reveal_giua();
    test_type_reveal_bang_do_dai();
    test_type_reveal_vuot_do_dai();
    test_type_chuoi_dai_bi_cat();
    test_info_line_day_du();
    test_info_line_dang_decode();
    test_info_reveal();
    printf("tất cả test qua\n");
    return 0;
}
