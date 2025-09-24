#include <stdio.h>
#include "hardware.h"

// 다른 파일에 있는 각 모듈의 초기화 함수를 호출하기 위해 선언
extern int camera_module_init();
extern int lcd_module_init();
extern int can_module_init();

int hardware_init() {
    printf("[MOCK BSP] > 하드웨어 전체 초기화 시작...\n");
    if (camera_module_init() != 0) return -1;
    if (lcd_module_init() != 0) return -1;
    if (can_module_init() != 0) return -1;
    printf("[MOCK BSP] > 모든 하드웨어 초기화 성공!\n");
    return 0;
}

void hardware_close() {
    printf("[MOCK BSP] > 하드웨어 전체 종료.\n");
}