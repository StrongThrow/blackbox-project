/**
 * @file can_receiver_main.c
 * @brief hardware.h API를 사용하여 CAN 메시지를 수신하는 간단한 테스트 프로그램.
 * @details
 * 이 프로그램은 CAN 버스를 초기화하고, 무한 루프를 돌면서 주기적으로
 * 새 메시지가 있는지 확인합니다. 메시지가 수신되면 해당 내용을 터미널에 출력합니다.
 */

#include <stdio.h>    // printf, fprintf 함수 사용
#include <unistd.h>   // usleep 함수 사용 (마이크로초 단위 대기)
#include "hardware.h" // 우리가 사용할 can_init, can_receive_message 등의 API가 정의된 헤더

// --- 프로그램 시작점 ---
int main() {
    // 1. CAN 버스 초기화
    // "can0" 인터페이스를 사용하도록 초기화합니다.
    // 실패 시 음수 값을 반환합니다.
    if (can_init("can0") < 0) {
        fprintf(stderr, "Error: Failed to initialize CAN bus 'can0'.\n");
        return 1; // 오류 코드를 반환하며 프로그램 종료
    }
    printf("CAN bus 'can0' initialized. Waiting for messages...\n");

    // 2. 무한 루프를 돌며 메시지 수신 확인
    while (1) {
        CANMessage msg; // 수신된 메시지를 저장할 구조체 변수
        
        // 3. CAN 메시지 수신 시도
        // 이 함수는 논블로킹(Non-blocking)이므로, 메시지가 없으면 기다리지 않고 즉시 0을 반환합니다.
        int result = can_receive_message(&msg);

        // 4. can_receive_message의 반환 값에 따라 처리
        if (result == 1) {
            // --- 메시지 수신 성공! ---
            printf("================================\n");
            printf(" CAN Message Received!\n");
            // %03X는 16진수(Hex)를 3자리로, 앞을 0으로 채워서 출력 (예: 0x1A -> 01A)
            printf("  ID  : 0x%03X\n", msg.id);
            printf("  DLC : %d\n", msg.dlc); // DLC는 데이터 길이 (0~8)
            
            printf("  Data: ");
            for (int i = 0; i < msg.dlc; ++i) {
                // %02X는 16진수를 2자리로, 앞을 0으로 채워서 출력 (예: 0xF -> 0F)
                printf("%02X ", msg.data[i]);
            }
            printf("\n");
            printf("================================\n\n");

        } else if (result < 0) {
            // --- 에러 발생 ---
            fprintf(stderr, "Error receiving CAN message.\n");
            break; // 에러 발생 시 루프를 종료합니다.
        }
        // result == 0 인 경우는 '수신된 메시지 없음'이므로 아무 작업도 하지 않고 넘어갑니다.

        // 5. CPU 자원을 100% 사용하지 않도록 잠시 대기
        // 루프가 너무 빨리 돌면 CPU 점유율이 100%가 되므로, 짧은 시간 쉬어줍니다.
        // 10,000 마이크로초 = 10 밀리초 (0.01초)
        usleep(10000);
    }

    // 6. 프로그램 종료 전 CAN 버스 닫기
    // 위 while(1) 루프에서는 이 코드가 실행되지 않지만,
    // break 등으로 루프를 탈출했을 때를 대비한 정리 코드입니다.
    printf("Closing CAN bus.\n");
    can_close();

    return 0;
}