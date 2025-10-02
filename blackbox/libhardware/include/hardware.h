#ifndef HARDWARE_H
#define HARDWARE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// ================= 1. 공통 및 초기화 API =================

// --- CAN 통신 관련 변수 ---
#define PID_ENGINE_SPEED            0x0c //업계 표준, RPM 
#define PID_VEHICLE_SPEED           0x0d //업계 표준, 속도
#define PID_GEAR_STATE              0xa4 //업계 표준(특정 차에서는 안될수도 있음)
#define PID_GPS_XDATA               0x10 //GPS 데이터(실제 존재 x)
#define PID_GPS_YDATA               0x11 //GPS 데이터(실제 존재 x)
#define PID_STEERING_DATA           0x20 //조향각 데이터(실제 존재 X)
#define PID_BRAKE_DATA              0x40 //브레이크 데이터(실제 존재 X)
#define PID_TIRE_DATA               0x80 //타이어 공기압 데이터(실제 존재 X)

// --- 상태 제어 관련 변수 ---
#define ENGINE_SPEED_FLAG           0x01
#define VEHICLE_SPEED_FLAG          0x02
#define GEAR_STATE_FLAG             0x04
#define GPS_XDATA_FLAG              0x08
#define GPS_YDATA_FLAG              0x10
#define STEERING_DATA_FLAG          0x20
#define BRAKE_DATA_FLAG             0x40
#define TIRE_DATA_FLAG              0x80

typedef struct{
    unsigned char pid; // 요청할 PID
    unsigned char flag; //PID 응답 저장 플래그
}CANRequest;

#define AI_REQUEST_FLAG             0x01

#define GPS_AVAILABLE               (GPS_XDATA_FLAG|GPS_YDATA_FLAG)
#define AI_AVAILABLE                (GPS_XDATA_FLAG|GPS_YDATA_FLAG|STEERING_DATA_FLAG)
#define COMPLETE_DATA_FLAG          (ENGINE_SPEED_FLAG|VEHICLE_SPEED_FLAG|GEAR_STATE_FLAG|GPS_XDATA_FLAG|GPS_YDATA_FLAG|STEERING_DATA_FLAG|BRAKE_DATA_FLAG|TIRE_DATA_FLAG)
#define DATA_AVAILABLE              (COMPLETE_DATA_FLAG & (~AI_AVAILABLE))



// ================= 2. 카메라 API =================
typedef struct {
    unsigned char* data; // RGB24
    int width;
    int height;
    size_t size;          // bytes
    void* private_data;  // 내부 상태 포인터(옵션)
} FrameBuffer;

FrameBuffer* camera_get_frame();
void camera_release_frame(FrameBuffer* frame);

// ================= 3. 그래픽 렌더링 API =================
void graphics_draw_rectangle(FrameBuffer* frame, int x, int y, int w, int h, int thickness, unsigned int color);
void graphics_draw_text(FrameBuffer* frame, const char* text, int x, int y, int font_size, unsigned int color);

// ================= 4. LCD 디스플레이 API =================
int lcd_display_frame(const FrameBuffer* frame);

// ================= 5. 저장 장치 API =================
int storage_start_recording(const char* filename);
void storage_stop_recording();
int storage_write_frame(const FrameBuffer* frame);

// ================= 6. CAN 통신 API =================
typedef struct {
    unsigned int id;
    unsigned char dlc;
    unsigned char data[8];
} CANMessage;

typedef struct {
    double gps_x;
    double gps_y;
    int speed;
    int rpm;
    unsigned char brake_state;
    float gear_ratio;
    char gear_state;
    float degree;
    unsigned char throttle;
    unsigned char tire_pressure[4];
} VehicleData;

int can_request_pid(unsigned char pid);
void can_parse_and_update_data(const CANMessage* msg, VehicleData* vehicle_data, unsigned char* flag);
int can_init(const char* interface_name); // <<-- 수정: 인터페이스 이름을 받고, 성공 시 fd를 반환하도록 변경
int can_send_message(const CANMessage* msg);
int can_receive_message(CANMessage* msg); // 1=수신, 0=없음, <0=에러
void can_close();

#ifdef __cplusplus
}
#endif
#endif
