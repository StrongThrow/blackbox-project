#include <stdio.h>
#include <string.h>     // (선택) 패턴 채우기용
#include "hardware.h"

static FrameBuffer g_mock_frame;
static unsigned char g_mock_image_data[640 * 480 * 3];

int camera_module_init() {
    g_mock_frame.width  = 640;
    g_mock_frame.height = 480;
    g_mock_frame.data   = g_mock_image_data;
    g_mock_frame.size   = (size_t)g_mock_frame.width * (size_t)g_mock_frame.height * 3u; // ★ 핵심
    g_mock_frame.private_data = NULL;

    // (선택) 화면에 뭔가 보이게 RGB 패턴 채우기
    // memset(g_mock_image_data, 0x40, g_mock_frame.size);

    printf("[MOCK CAMERA] > 카메라 모듈 초기화 성공\n");
    return 0;
}

FrameBuffer* camera_get_frame() {
    printf("[MOCK CAMERA] > 프레임 획득\n");

    // (선택) 프레임마다 size를 재확인/보수
    g_mock_frame.size = (size_t)g_mock_frame.width * (size_t)g_mock_frame.height * 3u;

    // (선택) 간단한 움직임 패턴
    // static unsigned v; v = (v + 1) & 0xFF;
    // for(size_t i=0; i<g_mock_frame.size; i+=3) {
    //     g_mock_image_data[i+0] = v;       // R
    //     g_mock_image_data[i+1] = 255-v;   // G
    //     g_mock_image_data[i+2] = 0;       // B
    // }

    return &g_mock_frame;
}

void camera_release_frame(FrameBuffer* frame) { /* no-op */ }

void graphics_draw_rectangle(FrameBuffer* f, int x, int y, int w, int h, int t, unsigned int c) {
    printf("[MOCK GFX] > 사각형 그리기: (%d,%d)\n", x, y);
}

void graphics_draw_text(FrameBuffer* f, const char* text, int x, int y, int size, unsigned int color) {
    printf("[MOCK GFX] > 텍스트 그리기: \"%s\"\n", text);
}
