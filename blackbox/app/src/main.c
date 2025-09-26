// main.c — 헤더에 선언된 API만 사용한 최소 동작 예시
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "hardware.h"

#define CAN_BITRATE       500000
#define REC_TRIG_SPEED    30          // km/h 초과 시 녹화 시작(예시)
#define TEXT_COLOR        0xFFFFFFFF  // ARGB 가정(흰색)
#define BOX_COLOR         0xFF00FF00  // ARGB 가정(연두색)

static volatile sig_atomic_t g_running = 1;

static void on_sigint(int signo) {
    (void)signo;
    g_running = 0;
}

// 예시: 0x2B0 id의 2~3바이트에 0.01 단위 속도라고 가정 (차량별로 조정)
static int parse_speed_from_can(const CANMessage* msg) {
    if (!msg) return -1;
    if (msg->id == 0x2B0 && msg->dlc >= 4) {
        int raw = (msg->data[2] << 8) | msg->data[3];
        return (int)(raw * 0.01); // 0.01 스케일 → km/h
    }
    return -1;
}

static void make_record_filename(char* out, size_t n) {
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(out, n, "/data/records/%04d%02d%02d_%02d%02d%02d.mp4",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

int main(void) {
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    printf("[APP] start\n");

    // 1) CAN 초기화(헤더에 있는 유일한 초기화성 API)
    if (can_init(CAN_BITRATE) < 0) {
        fprintf(stderr, "[APP] CAN init failed\n");
        // 카메라/LCD/스토리지는 헤더상 별도 init API가 없음 → 계속 진행
    }

    int cur_speed = 0;
    int is_recording = 0;
    char recfile[256] = {0};

    while (g_running) {
        // 2) CAN 수신(논블로킹 가정)
        CANMessage m;
        int cr = can_receive_message(&m);
        if (cr == 1) {
            int sp = parse_speed_from_can(&m);
            if (sp >= 0) cur_speed = sp;
        }

        // 3) 카메라 프레임 획득
        FrameBuffer* f = camera_get_frame();
        if (!f) {
            usleep(5000);
            continue;
        }

        // 4) 오버레이(예시: 속도 텍스트 & 프레임 모서리 사각형)
        char info[64];
        snprintf(info, sizeof(info), "Speed: %d km/h", cur_speed);
        graphics_draw_text(f, info, 10, 28, 24, TEXT_COLOR);
        graphics_draw_rectangle(f, 5, 5, f->width - 10, f->height - 10, 2, BOX_COLOR);

        // 5) LCD 출력
        (void)lcd_display_frame(f);

        // 6) 이벤트 기반 녹화(예시 로직)
        if (!is_recording && cur_speed > REC_TRIG_SPEED) {
            make_record_filename(recfile, sizeof(recfile));
            if (storage_start_recording(recfile) == 0) {
                is_recording = 1;
                printf("[APP] recording start: %s\n", recfile);
            } else {
                fprintf(stderr, "[APP] recording start failed\n");
            }
        }
        if (is_recording) {
            if (storage_write_frame(f) < 0) {
                fprintf(stderr, "[APP] recording write failed → stop\n");
                storage_stop_recording();
                is_recording = 0;
            }
        }

        // 7) 프레임 반납
        camera_release_frame(f);
    }

    // 8) 종료 처리
    if (is_recording) {
        storage_stop_recording();
        is_recording = 0;
    }
    can_close();

    printf("[APP] bye\n");
    return 0;
}
