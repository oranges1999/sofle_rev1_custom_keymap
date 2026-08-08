// Copyright 2024 Santosh Kumar (@santosh)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define TRI_LAYER_LOWER_LAYER 2
#define TRI_LAYER_UPPER_LAYER 3
#define TRI_LAYER_ADJUST_LAYER 4

#define OLED_DISPLAY_128X32

// Đồng bộ timestamp hoạt động ma trận từ master sang slave. Không có nó thì
// nửa phải không biết phím nào được gõ, vì process_record_user chỉ chạy trên
// master.
#define SPLIT_ACTIVITY_ENABLE
