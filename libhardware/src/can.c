#include <stdio.h>
#include "hardware.h"

int can_module_init() {
    printf("[MOCK CAN] > CAN 모듈 초기화 성공\n");
    return 0;
}
int can_receive_message(CANMessage* msg) {
    static int counter = 0;
    if (++counter % 5 == 0) { // 5번에 한번씩 메시지 수신 흉내
        msg->id = 0x2B0; msg->dlc = 8;
        return 1;
    }
    return 0;
}