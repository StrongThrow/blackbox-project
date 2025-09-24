#ifndef HARDWARE_H
#define HARDWARE_H

// ... (이전에 확정된 모든 함수 선언 및 구조체 정의) ...
typedef struct {
    unsigned char* data; // 실제 이미지 데이터 포인터 (RGB24 형식)
    int width;           // 이미지 너비 (픽셀)
    int height;          // 이미지 높이 (픽셀)
    size_t size;            // 데이터 전체 크기 (bytes)
    void* private_data;  // 라이브러리 내부에서 상태 관리를 위해 사용하는 데이터
} FrameBuffer;

typedef struct { unsigned int id; unsigned char dlc; unsigned char data[8]; } CANMessage;

int hardware_init();
void hardware_close();
FrameBuffer* camera_get_frame();
void camera_release_frame(FrameBuffer* frame);
void graphics_draw_rectangle(FrameBuffer* frame, int x, int y, int w, int h, int thickness, unsigned int color);
void graphics_draw_text(FrameBuffer* frame, const char* text, int x, int y, int font_size, unsigned int color);
int lcd_display_frame(const FrameBuffer* frame);
int storage_start_recording(const char* filename);
void storage_stop_recording();
int can_receive_message(CANMessage* msg);

#endif