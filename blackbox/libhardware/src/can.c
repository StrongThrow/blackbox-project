/**
 * @file can.c
 * @brief SocketCAN을 사용하여 CAN 통신 기능을 구현하는 소스 파일.
 * @details
 * 이 파일은 hardware.h에 정의된 CAN API의 실제 동작을 정의합니다.
 * 리눅스 커널이 제공하는 표준 CAN 소켓 인터페이스를 사용하므로,
 * CAN 하드웨어가 리눅스에 올바르게 인식되어 있다면 어떤 장치에서든 동작합니다.
 * 핵심 특징은 '논블로킹(Non-blocking)' 모드로 동작하여, 메인 프로그램의 다른 작업을
 * 방해하지 않고 효율적으로 메시지를 수신할 수 있다는 점입니다.
 */

// --- 1. 필수 헤더 파일 포함 ---
#include <stdio.h>      // 표준 입출력 함수 (perror)
#include <string.h>     // 문자열 및 메모리 처리 함수 (strncpy, memcpy)
#include <unistd.h>     // 유닉스 표준(POSIX) API (close, write, read)
#include <fcntl.h>      // 파일 제어 함수 (fcntl)
#include <sys/ioctl.h>  // 입출력 제어 함수 (ioctl)
#include <sys/socket.h> // 소켓 프로그래밍 함수 (socket, bind)
#include <net/if.h>     // 네트워크 인터페이스 구조체 (ifreq)
#include <linux/can.h>  // 리눅스 CAN 프로토콜 관련 정의 (PF_CAN, CAN_RAW, sockaddr_can, can_frame)
#include <linux/can/raw.h>// CAN RAW 소켓 관련 정의

#include "hardware.h"   // 이 파일에서 구현할 함수의 원형이 담긴 헤더

// --- 2. 내부 전역 변수 ---
// 'static' 키워드는 이 변수가 can.c 파일 내부에서만 접근 가능하다는 것을 의미합니다. (캡슐화)
// CAN 통신에 사용될 소켓의 파일 디스크립터(fd)를 저장합니다.
// -1은 아직 초기화되지 않았거나 유효하지 않은 상태임을 나타내는 일반적인 관례입니다.
static int s_can_fd = -1;

/**
 * @brief CAN 인터페이스를 초기화하고 소켓을 준비합니다.
 * @param bitrate 비트레이트. 현재 구현에서는 이 값은 무시됩니다 (systemd 등 외부에서 설정).
 * @return 성공 시 0, 실패 시 -1을 반환합니다.
 */
int can_init(int bitrate) {
    // (void)bitrate; 는 컴파일러에게 "bitrate 변수를 의도적으로 사용하지 않았으니 경고를 띄우지 마세요"
    // 라고 알려주는 C언어의 관용적인 표현입니다.
    (void)bitrate;

    // --- 2-1. CAN 소켓 생성 ---
    // socket(프로토콜 체계, 소켓 타입, 프로토콜)
    // PF_CAN: CAN 통신을 위한 프로토콜 체계를 사용하겠습니다.
    // SOCK_RAW: 로우 레벨 프로토콜에 직접 접근하는 소켓을 만들겠습니다.
    // CAN_RAW: RAW CAN 프레임을 직접 주고받는 프로토콜을 사용하겠습니다.
    s_can_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s_can_fd < 0) {
        perror("socket(PF_CAN) error"); // 소켓 생성 실패 시 에러 메시지 출력
        return -1;
    }

    // --- 2-2. 사용할 CAN 인터페이스 지정 ("can0") ---
    // ifreq 구조체는 네트워크 인터페이스의 정보를 담는 데 사용됩니다.
    struct ifreq ifr = {0};
    // "can0"라는 이름의 CAN 인터페이스를 사용하도록 이름을 복사합니다.
    strncpy(ifr.ifr_name, "can0", IFNAMSIZ - 1);

    // ioctl()은 디바이스를 제어하는 시스템 콜입니다.
    // SIOCGIFINDEX는 '인터페이스 이름(ifr.ifr_name)에 해당하는 시스템 내부의 고유 인덱스 번호를 알려주세요' 라는 요청입니다.
    if (ioctl(s_can_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX) error");
        close(s_can_fd); s_can_fd = -1; // 실패 시 자원 정리
        return -1;
    }

    // --- 2-3. 소켓과 CAN 인터페이스 바인딩(연결) ---
    // sockaddr_can 구조체는 CAN 소켓의 주소 정보를 담습니다.
    struct sockaddr_can addr = {0};
    addr.can_family = AF_CAN; // 주소 체계는 CAN
    addr.can_ifindex = ifr.ifr_ifindex; // 위에서 얻어온 "can0"의 인덱스 번호

    // bind() 함수는 생성된 소켓(s_can_fd)을 특정 주소(addr)에 할당(연결)합니다.
    // "이 소켓은 이제부터 can0 인터페이스를 통해 통신할 것입니다."
    if (bind(s_can_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind(can0) error");
        close(s_can_fd); s_can_fd = -1; // 실패 시 자원 정리
        return -1;
    }
    
    // --- 2-4. 소켓을 논블로킹(Non-blocking) 모드로 설정 ---
    // fcntl()은 파일 디스크립터의 속성을 제어하는 함수입니다.
    // F_GETFL: 현재 파일 디스크립터의 속성 플래그를 가져옵니다.
    int flags = fcntl(s_can_fd, F_GETFL, 0);
    // F_SETFL: 기존 플래그에 O_NONBLOCK 플래그를 추가하여 새로운 속성을 설정합니다.
    // O_NONBLOCK이 설정되면 read() 함수가 읽을 데이터가 없을 때 기다리지 않고 즉시 리턴됩니다.
    fcntl(s_can_fd, F_SETFL, flags | O_NONBLOCK);
    
    return 0; // 모든 과정 성공
}

/**
 * @brief CAN 메시지를 전송합니다.
 * @param msg 전송할 메시지 데이터가 담긴 CANMessage 구조체 포인터.
 * @return 성공 시 0, 실패 시 -1.
 */
int can_send_message(const CANMessage* msg) {
    if (s_can_fd < 0 || !msg) return -1; // 소켓이 유효하지 않거나 msg가 NULL이면 실패

    // CANMessage (API용 구조체) -> can_frame (리눅스 커널용 구조체) 변환
    struct can_frame frame = {0};
    frame.can_id = msg->id;
    frame.can_dlc = msg->dlc;
    memcpy(frame.data, msg->data, frame.can_dlc); // 데이터 복사

    // write() 시스템 콜을 통해 소켓으로 데이터를 전송합니다.
    int n = write(s_can_fd, &frame, sizeof(frame));
    // 요청한 크기만큼 정확히 전송되었는지 확인합니다.
    return (n == (int)sizeof(frame)) ? 0 : -1;
}

/**
 * @brief CAN 메시지를 수신합니다. (논블로킹)
 * @param msg 수신된 메시지를 저장할 CANMessage 구조체 포인터.
 * @return 1: 메시지 수신 성공, 0: 수신된 메시지 없음, <0: 에러 발생.
 */
int can_receive_message(CANMessage* msg) {
    if (s_can_fd < 0 || !msg) return -1; // 소켓이 유효하지 않거나 msg가 NULL이면 실패
    
    struct can_frame frame;
    // read() 시스템 콜을 통해 소켓으로부터 데이터를 읽어옵니다.
    int n = read(s_can_fd, &frame, sizeof(frame));

    // [논블로킹 로직의 핵심]
    // 읽을 데이터가 없으면 read()는 -1을 반환하고 errno를 EAGAIN 또는 EWOULDBLOCK으로 설정합니다.
    // 이 코드에서는 간단히 음수 값을 '수신 없음'으로 간주하여 0을 반환합니다.
    if (n < 0) return 0;

    // 읽어온 데이터가 정상적인 can_frame 크기보다 작으면 에러로 간주합니다.
    if (n < (int)sizeof(frame)) return -1;

    // can_frame (리눅스 커널용 구조체) -> CANMessage (API용 구조체) 변환
    msg->id = frame.can_id;
    msg->dlc = frame.can_dlc;
    memcpy(msg->data, frame.data, frame.can_dlc); // 데이터 복사
    
    return 1; // 메시지 1개 수신 성공
}

/**
 * @brief CAN 소켓을 닫고 자원을 정리합니다.
 */
void can_close() {
    // 소켓이 유효한 경우(-1이 아닌 경우)에만 닫기 작업을 수행합니다.
    if (s_can_fd >= 0) {
        close(s_can_fd);
        s_can_fd = -1; // 다시 유효하지 않은 상태로 설정
    }
}