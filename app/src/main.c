/**
 * @file main.c
 * @brief 지능형 블랙박스 시스템의 메인 컨트롤러 애플리케이션
 * @version 1.2
 * @date 2025-09-24
 *
 * 변경점 (v1.2):
 * - Python→C 통신을 "길이 프레이밍(4B len + JSON payload)"로 변경
 * - stderr를 stdout 파이프에 합치지 않음(로그 분리)
 * - read_exact / write_exact 도입으로 부분 입출력 안전화
 * - 프레임 전송 시 width*height*3(RGB888) 바이트로 강제 일치
 * - 재시작/에러처리 메시지 개선
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>

#include "cJSON.h" // JSON 파싱 라이브러리
#include "hardware.h"    // BSP팀 제공 API 명세서

#define AI_SERVER_COMMAND "./ai/vision_server.py"
#define JSON_BUFFER_SIZE 2048

// --- AI 프로세스 핸들 ---
typedef struct {
    pid_t pid;         // 자식 PID
    FILE* to_child;    // 부모 -> 자식 (stdin) : fwrite 사용 (binary)
    FILE* from_child;  // 자식 -> 부모 (stdout) : fread 사용 (length framing)
} AIProcess;

// --- Helper Prototypes ---
AIProcess* start_ai_server(void);
void stop_ai_server(AIProcess* proc);
int parse_speed_from_can(const CANMessage* msg);
void process_ai_results(FrameBuffer* frame, const char* json_string, int* out_person_count);
int is_dangerous_situation(int speed, int person_count);

static int read_exact(FILE* f, void* buf, size_t n);
static int write_exact(FILE* f, const void* buf, size_t n);

// =================================================================
//                          Main
// =================================================================
int main(void) {
    // SIGPIPE 무시: 자식이 죽었을 때 쓰기 중단 방지
    signal(SIGPIPE, SIG_IGN);

    printf("[APP] 블랙박스 시스템을 시작합니다...\n");

    // --- 1. 초기화 ---
    if (hardware_init() != 0) {
        fprintf(stderr, "[APP] 치명적 오류: 하드웨어 초기화 실패!\n");
        return -1;
    }

    AIProcess* ai_proc = start_ai_server();
    if (ai_proc == NULL) {
        hardware_close();
        return -1;
    }

    int current_vehicle_speed = 0;
    int is_recording = 0;
    CANMessage can_msg;

    printf("[APP] 시스템 준비 완료. 메인 루프를 시작합니다.\n");

    while (1) {
        // --- 2.1. CAN 데이터 수신 ---
        if (can_receive_message(&can_msg) == 1) {
            int sp = parse_speed_from_can(&can_msg);
            if (sp >= 0) current_vehicle_speed = sp;
        }

        // --- 2.2. 카메라 프레임 획득 ---
        FrameBuffer* frame = camera_get_frame();
        if (frame == NULL) {
            usleep(10000); // 10ms 대기 후 다시 시도
            continue;
        }

        // --- 2.3. AI 서버에 분석 요청 (C -> Python) ---
        if (ai_proc && ai_proc->to_child) {
            int w = frame->width;
            int h = frame->height;
            size_t expected = (size_t)w * (size_t)h * 3u;  // RGB888 전제
            if (write_exact(ai_proc->to_child, &w, sizeof(int)) != 1 ||
                write_exact(ai_proc->to_child, &h, sizeof(int)) != 1) {
                fprintf(stderr, "[APP] 오류: 프레임 헤더 전송 실패\n");
            } else {
                // 주의: 카메라 버퍼 포맷이 RGB888이 아니라면 전처리 변환 필요
                if (frame->size < expected) {
                    fprintf(stderr, "[APP] 경고: frame->size(%zu) < expected(%zu). 포맷/크기 확인 필요\n",
                            frame->size, expected);
                }
                size_t payload = expected;
                if (write_exact(ai_proc->to_child, frame->data, payload) != 1) {
                    fprintf(stderr, "[APP] 경고: 프레임 바디 전송 실패\n");
                }
                fflush(ai_proc->to_child);
            }
        } else {
            fprintf(stderr, "[APP] 경고: AI 프로세스 파이프(to_child) 무효. 재시작 시도...\n");
        }

        // --- 2.4. AI 서버로부터 결과 수신 (Python -> C) ---
        int got_result = 0;
        int person_count = 0;
        char* json_dyn = NULL;
        char  json_static[JSON_BUFFER_SIZE];

        if (ai_proc && ai_proc->from_child) {
            uint32_t jlen_le = 0;
            int rr = read_exact(ai_proc->from_child, &jlen_le, sizeof(jlen_le));
            if (rr != 1) {
                fprintf(stderr, "[APP] 경고: 길이 읽기 실패/EOF. 자식 PID=%d. 재시작 시도...\n", ai_proc->pid);
                stop_ai_server(ai_proc);
                sleep(1);
                ai_proc = start_ai_server();
                camera_release_frame(frame);
                if (!ai_proc) break;
                continue;
            }

            uint32_t jlen = jlen_le; // 동일 리틀엔디안 가정
            if (jlen == 0 || jlen > (64 * 1024 * 1024)) { // 상한 64MB
                fprintf(stderr, "[APP] 경고: 비정상 JSON 길이(%u). 재시작.\n", jlen);
                stop_ai_server(ai_proc);
                sleep(1);
                ai_proc = start_ai_server();
                camera_release_frame(frame);
                if (!ai_proc) break;
                continue;
            }

            char* dst = NULL;
            if (jlen < JSON_BUFFER_SIZE) {
                dst = json_static;
            } else {
                dst = (char*)malloc(jlen + 1);
                if (!dst) {
                    fprintf(stderr, "[APP] 치명적: JSON 메모리(%u) 할당 실패\n", jlen);
                    camera_release_frame(frame);
                    break;
                }
                json_dyn = dst;
            }

            if (read_exact(ai_proc->from_child, dst, jlen) != 1) {
                fprintf(stderr, "[APP] 경고: JSON 본문 읽기 실패/EOF. 재시작.\n");
                if (json_dyn) { free(json_dyn); json_dyn = NULL; }
                stop_ai_server(ai_proc);
                sleep(1);
                ai_proc = start_ai_server();
                camera_release_frame(frame);
                if (!ai_proc) break;
                continue;
            }
            dst[jlen] = '\0';
            got_result = 1;

            if (got_result) {
                process_ai_results(frame, dst, &person_count);
            }
            if (json_dyn) { free(json_dyn); json_dyn = NULL; }
        } else {
            fprintf(stderr, "[APP] 경고: AI 프로세스 핸들(from_child) 없음. 재시작.\n");
            if (ai_proc) stop_ai_server(ai_proc);
            ai_proc = start_ai_server();
            camera_release_frame(frame);
            if (!ai_proc) break;
            continue;
        }

        // --- 2.6. CAN 데이터 렌더링 ---
        char info_text[100];
        snprintf(info_text, sizeof(info_text), "Speed: %d km/h", current_vehicle_speed);
        graphics_draw_text(frame, info_text, 10, 30, 24, 0xFFFFFFFF);

        // --- 2.7. LCD 출력 ---
        lcd_display_frame(frame);

        // --- 2.8. 위험 상황 판단 및 녹화 ---
        if (is_dangerous_situation(current_vehicle_speed, person_count) && !is_recording) {
            printf("[APP] 위험 상황 감지! 녹화를 시작합니다.\n");
            // storage_start_recording("..."); // 실제 구현 위치
            is_recording = 1;
        }

        // --- 2.9. 프레임 자원 해제 ---
        camera_release_frame(frame);
    }

    printf("[APP] 시스템을 종료합니다...\n");
    if (ai_proc) stop_ai_server(ai_proc);
    hardware_close();
    return 0;
}

// =================================================================
//                      Helper 구현
// =================================================================

/**
 * @brief 정확히 n바이트를 읽는다. 성공=1, EOF=0, 에러=-1
 */
static int read_exact(FILE* f, void* buf, size_t n) {
    uint8_t* p = (uint8_t*)buf;
    size_t got = 0;
    while (got < n) {
        size_t r = fread(p + got, 1, n - got, f);
        if (r == 0) {
            if (feof(f)) return 0;
            if (ferror(f)) return -1;
            // 이론상 여기 오기 어려움
        }
        got += r;
    }
    return 1;
}

/**
 * @brief 정확히 n바이트를 쓴다. 성공=1, 실패=-1
 */
static int write_exact(FILE* f, const void* buf, size_t n) {
    const uint8_t* p = (const uint8_t*)buf;
    size_t sent = 0;
    while (sent < n) {
        size_t w = fwrite(p + sent, 1, n - sent, f);
        if (w == 0) return -1;
        sent += w;
    }
    return 1;
}

/**
 * @brief AI 서버를 시작하고 양방향 통신 파이프를 설정한다.
 *
 * parent_to_child: 부모(쓰기) -> 자식(읽기) (stdin)
 * child_to_parent: 자식(쓰기 stdout) -> 부모(읽기)
 * stderr는 stdout에 합치지 않음(분리)
 */
AIProcess* start_ai_server(void) {
    int parent_to_child[2];
    int child_to_parent[2];

    if (pipe(parent_to_child) == -1) {
        perror("[APP] pipe(parent_to_child) 실패");
        return NULL;
    }
    if (pipe(child_to_parent) == -1) {
        perror("[APP] pipe(child_to_parent) 실패");
        close(parent_to_child[0]); close(parent_to_child[1]);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("[APP] fork 실패");
        close(parent_to_child[0]); close(parent_to_child[1]);
        close(child_to_parent[0]); close(child_to_parent[1]);
        return NULL;
    }

    if (pid == 0) {
        // ===== 자식 프로세스 =====
        // stdin
        if (dup2(parent_to_child[0], STDIN_FILENO) == -1) {
            perror("[CHILD] dup2 stdin 실패");
            _exit(1);
        }
        // stdout
        if (dup2(child_to_parent[1], STDOUT_FILENO) == -1) {
            perror("[CHILD] dup2 stdout 실패");
            _exit(1);
        }

        // stderr는 기본(부모의 stderr로) 두거나, 별도 파일로 리다이렉트 가능
        // 예: 파일 로그를 원하면 아래 주석 해제
        /*
        int fd = open("/tmp/ai_server.log", O_CREAT|O_WRONLY|O_APPEND, 0644);
        if (fd != -1) {
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        */

        // 자식에서 불필요한 파이프 끝 닫기
        close(parent_to_child[0]); close(parent_to_child[1]);
        close(child_to_parent[0]); close(child_to_parent[1]);

        // 실행
        execlp("python3", "python3", AI_SERVER_COMMAND, (char*)NULL);

        perror("[CHILD] execlp 실패");
        _exit(1);
    } else {
        // ===== 부모 프로세스 =====
        close(parent_to_child[0]);   // 부모는 읽기 끝 안 씀
        close(child_to_parent[1]);   // 부모는 쓰기 끝 안 씀

        FILE* to_child = fdopen(parent_to_child[1], "wb");
        if (!to_child) {
            perror("[APP] fdopen(to_child) 실패");
            close(parent_to_child[1]);
            close(child_to_parent[0]);
            int st; waitpid(pid, &st, 0);
            return NULL;
        }

        FILE* from_child = fdopen(child_to_parent[0], "rb");
        if (!from_child) {
            perror("[APP] fdopen(from_child) 실패");
            fclose(to_child);
            close(child_to_parent[0]);
            int st; waitpid(pid, &st, 0);
            return NULL;
        }

        // 읽기는 버퍼링 그대로( fread 기반 ), 쓰기는 fflush로 보장
        AIProcess* proc = (AIProcess*)malloc(sizeof(AIProcess));
        if (!proc) {
            fprintf(stderr, "[APP] 메모리 할당 실패\n");
            fclose(to_child); fclose(from_child);
            int st; waitpid(pid, &st, 0);
            return NULL;
        }
        proc->pid = pid;
        proc->to_child = to_child;
        proc->from_child = from_child;

        printf("[APP] AI 서버 시작 (PID=%d)\n", pid);
        return proc;
    }
}

void stop_ai_server(AIProcess* proc) {
    if (proc == NULL) return;

    if (proc->to_child)  { fclose(proc->to_child);  proc->to_child  = NULL; }
    if (proc->from_child){ fclose(proc->from_child);proc->from_child= NULL; }

    int status = 0;
    pid_t r = waitpid(proc->pid, &status, WNOHANG);
    if (r == 0) {
        kill(proc->pid, SIGTERM);
        waitpid(proc->pid, &status, 0);
    }
    printf("[APP] AI 서버 (PID=%d) 종료 처리 완료. status=%d\n", proc->pid, status);
    free(proc);
}

/** @brief 수신된 CAN 메시지로부터 차량 속도를 파싱 (차량별로 수정 필요) */
int parse_speed_from_can(const CANMessage* msg) {
    if (msg->id == 0x2B0) { // 예시 ID
        int speed_raw = (msg->data[2] << 8) | msg->data[3];
        return (int)(speed_raw * 0.01); // 0.01 스케일
    }
    return -1;
}

/**
 * @brief JSON 파싱 및 박스 드로잉 / person_count 집계
 */
void process_ai_results(FrameBuffer* frame, const char* json_string, int* out_person_count) {
    if (out_person_count) *out_person_count = 0;
    if (!json_string) return;

    cJSON *root = cJSON_Parse(json_string);
    if (!root) {
        fprintf(stderr, "[APP] cJSON_Parse 실패\n");
        return;
    }

    cJSON *detections = cJSON_GetObjectItem(root, "detections");
    if (!cJSON_IsArray(detections)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *detection = NULL;
    cJSON_ArrayForEach(detection, detections) {
        cJSON *class_name = cJSON_GetObjectItem(detection, "class_name");
        cJSON *box = cJSON_GetObjectItem(detection, "box");
        if (!cJSON_IsString(class_name) || !cJSON_IsArray(box) || cJSON_GetArraySize(box) != 4)
            continue;

        int x = cJSON_GetArrayItem(box, 0)->valueint;
        int y = cJSON_GetArrayItem(box, 1)->valueint;
        int w = cJSON_GetArrayItem(box, 2)->valueint;
        int h = cJSON_GetArrayItem(box, 3)->valueint;

        unsigned int color = 0xFF0000FF; // 파란색
        if (strcmp(class_name->valuestring, "person") == 0) {
            color = 0xFFFF0000; // 빨간색
            if (out_person_count) (*out_person_count)++;
        }
        graphics_draw_rectangle(frame, x, y, w, h, 2, color);
    }

    cJSON_Delete(root);
}

/** @brief 단순 위험 판단 로직 */
int is_dangerous_situation(int speed, int person_count) {
    return (speed > 30 && person_count > 0) ? 1 : 0;
}
