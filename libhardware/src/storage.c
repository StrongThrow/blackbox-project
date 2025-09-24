#include <stdio.h>
#include "hardware.h"

int storage_start_recording(const char* filename) {
    printf("[MOCK STORAGE] > '%s' 파일로 녹화 시작!\n", filename);
    return 0;
}
void storage_stop_recording() {
    printf("[MOCK STORAGE] > 녹화 중지!\n");
}