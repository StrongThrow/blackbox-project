/*
 * Arduino ECU Emulator (MCP2515, 8MHz, 500 kbps)
 * - Serial RX: [AA][55][90][LEN][PID][VALUE...][CRC8 XOR]
 *     * GPS_X/Y(VALUE) = float32(LE, meters)  <-- PC에서 struct.pack("<f", x_m)
 *     * SPEED(0x0D) = 1B km/h
 *     * RPM(0x0C)   = 2B BE (rpm*4)  // 응답에서 (A*256 + B)/4
 *     * GEAR(0xA4)  = 3B (ratio_x1000 BE, gear_code<<4)
 *     * STEER(0x20) = 2B (I, F)  // deg = I + F/100
 *     * BRAKE(0x40) = 1B (0/1)
 *     * TIRE(0x80)  = 4B (kPa 정수)
 *
 * - CAN RX:  0x7DF # 02 01 <PID> 00 00 00 00 00 (OBD Mode 01 request)
 * - CAN TX:  0x7E8 # [LEN][0x41][PID][payload...]
 *     * GPS_X(0x10) / GPS_Y(0x11): [07][41][PID][S][I][D2][D4][D6]
 *       → value = (S?+1:-1) * ( I + D2/100 + D4/10000 + D6/1000000 )
 *
 * 핀(UNO 예시):
 *   CS=10, INT=2, SCK=13, MOSI=11, MISO=12
 */

#include <SPI.h>
#include <mcp_can.h>
#include <math.h>

#define CAN_CS_PIN   10
#define CAN_INT_PIN   2

// MCP2515 설정
#define CAN_CLOCK    MCP_8MHZ
#define CAN_SPEED    CAN_500KBPS
#define CAN_MODE     MCP_ANY   // 일반 수신 (필터/마스크 미사용)

// OBD CAN ID
#define CAN_ID_REQ  0x7DF
#define CAN_ID_RES  0x7E8

// ====== PID 정의 (프로젝트 합의) ======
#define PID_RPM       0x0C
#define PID_SPEED     0x0D
#define PID_GEAR      0xA4
#define PID_GPS_X     0x10   // X (m), 시리얼로 float32(LE)
#define PID_GPS_Y     0x11   // Y (m), 시리얼로 float32(LE)
#define PID_STEER     0x20
#define PID_BRAKE     0x40
#define PID_TIRE      0x80

// ====== 시리얼 프레임 ======
#define SER_STX0  0xAA
#define SER_STX1  0x55
#define SER_MSG_PID_UPDATE 0x90

// 전역 상태(최신 값)
volatile uint8_t   g_speed = 0;          // km/h (0..255)
volatile uint16_t  g_rpm_x4 = 0;         // rpm*4 (응답은 A,B)
volatile uint16_t  g_ratio_x1000 = 0;    // 기어비*1000 (0..65535)
volatile uint8_t   g_gear_code = 0;      // 0:P,1:D,2:R... (상위니블로 응답)
volatile float     g_x_m = 0.0f;         // X(m)
volatile float     g_y_m = 0.0f;         // Y(m)
volatile uint8_t   g_steer_I = 0;        // 정수부
volatile uint8_t   g_steer_F = 0;        // 소수 2자리 (센티도)
volatile uint8_t   g_brake = 0;          // 0/1
volatile uint8_t   g_tp[4] = {230,230,235,240}; // 타이어압 예시(kPa 정수)

// MCP2515 인스턴스
MCP_CAN CAN0(CAN_CS_PIN);


// ====== 유틸 ======
static inline uint8_t clamp_u8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}
static inline uint8_t clamp_0_99(int v) {
  if (v < 0) return 0;
  if (v > 99) return 99;
  return (uint8_t)v;
}

// 간단 CRC8 XOR
uint8_t crc8_xor(const uint8_t* p, uint16_t n) {
  uint8_t c = 0;
  for (uint16_t i=0; i<n; ++i) c ^= p[i];
  return c;
}

// ====== 시리얼 FSM ======
enum RX_STATE { ST_WAIT_AA, ST_WAIT_55, ST_WAIT_ID, ST_WAIT_LEN, ST_WAIT_BODY, ST_WAIT_CRC };
RX_STATE rx_state = ST_WAIT_AA;
uint8_t  rx_id = 0;
uint8_t  rx_len = 0;
uint8_t  rx_buf[32];
uint8_t  rx_pos = 0;

void process_pid_update(const uint8_t* body, uint8_t len) {
  if (len < 1) return;
  uint8_t pid = body[0];
  const uint8_t* payload = body + 1;
  uint8_t plen = (len >= 1) ? (len - 1) : 0;

  switch (pid) {
    case PID_SPEED:
      if (plen >= 1) g_speed = payload[0];
      break;

    case PID_RPM:
      if (plen >= 2) {
        // rpm*4 (BE 2B)
        uint16_t v = ((uint16_t)payload[0] << 8) | payload[1];
        g_rpm_x4 = v;
      }
      break;

    case PID_GEAR:
      if (plen >= 3) {
        g_ratio_x1000 = ((uint16_t)payload[0] << 8) | payload[1];
        g_gear_code   = (payload[2] >> 4) & 0x0F;
      }
      break;

    case PID_GPS_X:
      if (plen >= 4) {
        float v = 0.0f;
        memcpy(&v, payload, 4);       // float32 LE (아두이노는 LE라 그대로 memcpy 가능)
        g_x_m = v;
      }
      break;

    case PID_GPS_Y:
      if (plen >= 4) {
        float v = 0.0f;
        memcpy(&v, payload, 4);       // float32 LE
        g_y_m = v;
      }
      break;

    case PID_STEER:
      if (plen >= 2) {
        g_steer_I = payload[0];
        g_steer_F = payload[1];
      }
      break;

    case PID_BRAKE:
      if (plen >= 1) g_brake = payload[0] ? 1 : 0;
      break;

    case PID_TIRE:
      if (plen >= 4) {
        g_tp[0]=payload[0]; g_tp[1]=payload[1]; g_tp[2]=payload[2]; g_tp[3]=payload[3];
      }
      break;

    default:
      // 미사용 PID 무시
      break;
  }
}

void serial_rx_fsm() {
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();
    switch (rx_state) {
      case ST_WAIT_AA:
        if (b == SER_STX0) rx_state = ST_WAIT_55;
        break;
      case ST_WAIT_55:
        if (b == SER_STX1) rx_state = ST_WAIT_ID;
        else rx_state = ST_WAIT_AA;
        break;
      case ST_WAIT_ID:
        rx_id = b; rx_state = ST_WAIT_LEN;
        break;
      case ST_WAIT_LEN:
        rx_len = b;
        if (rx_len > sizeof(rx_buf)) { rx_state = ST_WAIT_AA; break; }
        rx_pos = 0;
        rx_state = (rx_len > 0) ? ST_WAIT_BODY : ST_WAIT_CRC;
        break;
      case ST_WAIT_BODY:
        rx_buf[rx_pos++] = b;
        if (rx_pos >= rx_len) rx_state = ST_WAIT_CRC;
        break;
      case ST_WAIT_CRC: {
        uint8_t head[4] = { SER_STX0, SER_STX1, rx_id, rx_len };
        uint8_t c = crc8_xor(head, 4) ^ crc8_xor(rx_buf, rx_len);
        if (c == b && rx_id == SER_MSG_PID_UPDATE) {
          process_pid_update(rx_buf, rx_len);
        }
        rx_state = ST_WAIT_AA;
        break;
      }
    }
  }
}

// ====== 공통 OBD 응답 송신 ======
void send_obd_response(uint8_t pid, const uint8_t* payload, uint8_t plen) {
  uint8_t d[8] = {0};
  uint8_t L = 2 + plen;     // [0x41, PID] + payload
  if (L > 7) L = 7;         // 단일 프레임 최대 페이로드 5B → L=7까지
  d[0] = L;
  d[1] = 0x41;              // Mode 01 응답
  d[2] = pid;
  for (uint8_t i=0; i<plen && (3+i)<8; ++i) d[3+i] = payload[i];
  CAN0.sendMsgBuf(CAN_ID_RES, 0, 8, d);
}

// ====== GPS 값(S/I/D2/D4/D6) 엔코딩 ======
void encode_SI_D2D4D6(float meters, uint8_t out[5]) {
  float a = fabsf(meters);
  uint8_t S = (meters >= 0.0f) ? 1 : 0;

  // 정수부 0~255로 클램프 (범위 넘어가면 255로 포화)
  uint8_t I = (uint8_t) (a >= 255.0f ? 255 : (uint8_t)floorf(a));

  // 소수부 6자리로 변환
  float frac = a - (float)I;
  // 반올림 처리
  uint32_t frac6 = (uint32_t) lroundf(frac * 1000000.0f);
  if (frac6 > 999999) frac6 = 999999;

  uint8_t D2 = clamp_0_99( (int)( (frac6 / 10000) % 100 ) );
  uint8_t D4 = clamp_0_99( (int)( (frac6 /   100) % 100 ) );
  uint8_t D6 = clamp_0_99( (int)(  frac6          % 100 ) );

  out[0] = S;
  out[1] = I;
  out[2] = D2;
  out[3] = D4;
  out[4] = D6;
}

// ====== PID별 응답 ======
void respond_speed() {
  uint8_t p[1] = { g_speed };
  send_obd_response(PID_SPEED, p, 1);
}

void respond_rpm() {
  uint8_t A = (g_rpm_x4 >> 8) & 0xFF;
  uint8_t B = (g_rpm_x4     ) & 0xFF;
  uint8_t p[2] = { A, B };
  send_obd_response(PID_RPM, p, 2);
}

void respond_gear() {
  uint8_t A = (g_ratio_x1000 >> 8) & 0xFF;
  uint8_t B = (g_ratio_x1000     ) & 0xFF;
  uint8_t C = (g_gear_code & 0x0F) << 4;
  uint8_t p[3] = { A, B, C };
  send_obd_response(PID_GEAR, p, 3);
}

void respond_gps_x() {
  uint8_t p[5];
  encode_SI_D2D4D6(g_x_m, p);        // [S,I,D2,D4,D6]
  // LEN=2+5=7 → 단일프레임 가능
  send_obd_response(PID_GPS_X, p, 5);
}

void respond_gps_y() {
  uint8_t p[5];
  encode_SI_D2D4D6(g_y_m, p);
  send_obd_response(PID_GPS_Y, p, 5);
}

void respond_steer() {
  uint8_t p[2] = { g_steer_I, g_steer_F };
  send_obd_response(PID_STEER, p, 2);
}

void respond_brake() {
  uint8_t p[1] = { g_brake ? 1 : 0 };
  send_obd_response(PID_BRAKE, p, 1);
}

void respond_tire() {
  uint8_t p[4] = { g_tp[0], g_tp[1], g_tp[2], g_tp[3] };
  send_obd_response(PID_TIRE, p, 4);
}

// ====== CAN 요청 처리 ======
void handle_can_request(const uint8_t* buf, uint8_t len) {
  // 기대: [02][01][PID] ...
  if (len < 3) return;
  if (buf[0] < 0x02) return;   // 길이 체크(일반 OBD 단일 요청)
  if (buf[1] != 0x01) return;  // Mode 01만 처리
  uint8_t pid = buf[2];

  switch (pid) {
    case PID_SPEED: respond_speed(); break;
    case PID_RPM:   respond_rpm();   break;
    case PID_GEAR:  respond_gear();  break;
    case PID_GPS_X: respond_gps_x(); break;
    case PID_GPS_Y: respond_gps_y(); break;
    case PID_STEER: respond_steer(); break;
    case PID_BRAKE: respond_brake(); break;
    case PID_TIRE:  respond_tire();  break;
    default: /* 미지원 PID 무시 */   break;
  }
}

void setup() {
  Serial.begin(115200);

  // MCP2515 시작 (주의: begin 시그니처 3개 인자)
  while (CAN_OK != CAN0.begin(CAN_MODE, CAN_SPEED, CAN_CLOCK)) {
    delay(100);
  }
  // 필요시 마스크/필터 설정 가능. 여기선 전체 수신.
  // CAN0.init_Mask(0, 0, 0x00000000);
  // CAN0.init_Filt(0, 0, 0x00000000);

  pinMode(CAN_INT_PIN, INPUT);
  CAN0.setMode(MCP_NORMAL);
}

void loop() {
  // 1) 시리얼 수신 → 내부 상태 갱신
  serial_rx_fsm();

  // 2) CAN 요청 확인 → 응답
  if (CAN_MSGAVAIL == CAN0.checkReceive()) {
    long unsigned id; unsigned char len; unsigned char buf[8];
    CAN0.readMsgBuf(&id, &len, buf);
    if (id == CAN_ID_REQ) {
      handle_can_request(buf, len);
    }
  }
}
