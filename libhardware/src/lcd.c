#include <stdio.h>
#include "hardware.h"

int lcd_module_init() {
    printf("[MOCK LCD] > LCD 모듈 초기화 성공\n");
    return 0;
}
int lcd_display_frame(const FrameBuffer* frame) {
    printf("[MOCK LCD] > LCD에 프레임 출력\n");
    return 0;
}