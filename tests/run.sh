#!/bin/sh
# Biên dịch và chạy test host cho oled_cyberdeck.c.
set -e
cd "$(dirname "$0")/.."
gcc -std=c99 -Wall -Wno-unused-function -Wno-unused-variable -Itests -o /tmp/test_line tests/test_line.c
/tmp/test_line
