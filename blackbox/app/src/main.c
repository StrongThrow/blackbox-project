/**
 * @file main_final_version.c
 * @brief [최종 완성본] C와 Python을 연동하는 비동기 제어 시스템의 메인 프로그램.
 * @details
 * 이 프로그램은 우리가 논의한 모든 고급 기법을 포함합니다:
 * 1.  **프로세스 모델**: C가 부모(지휘자), Python이 자식(AI 분석 전문가)으로 동작합니다.
 * 2.  **프로세스 간 통신(IPC)**: 두 개의 파이프(pipe)를 사용해 안정적인 양방향 통신 채널을 구축합니다.
 * 3.  **동적 경로 탐색**: C 실행 파일의 위치를 기준으로 Python 스크립트의 절대 경로를 동적으로 계산하여,
 * 어디서 프로그램을 실행하든 경로 문제 없이 Python을 실행할 수 있습니다. (이식성/견고성 향상)
 * 4.  **비동기 I/O 처리**: 'select()' 시스템 콜을 사용하여 여러 입력 소스(Python의 응답, CAN 메시지 등)를
 * 하나의 스레드에서 효율적으로 동시에 감시하고 처리합니다. ('AI 분석 대기 중 CAN 통신' 요구사항 해결)
 * 5.  **상태 관리(State Management)**: 비동기적으로 도착하는 데이터들(AI 결과, CAN 메시지)을
 * 상태 변수에 저장했다가, 모든 데이터가 준비되었을 때만 최종 제어 로직을 수행합니다.
 *
 * @compile
 * gcc main_final_version.c cJSON.c -o main_app -lm
 * (cJSON.c와 cJSON.h 파일이 같은 폴더에 있어야 합니다.)
 *
 * @run
 * ./main_app
 * (종료하려면 터미널에서 Ctrl+C를 누르세요.)
 */

// --- 1. 필수 헤더 파일 포함 ---
#include <stdio.h>      // 표준 입출력 함수 (printf, perror, FILE*, fprintf, fflush, fgets)
#include <stdlib.h>     // 표준 라이브러리 함수 (exit, malloc, free)
#include <unistd.h>     // 유닉스 표준(POSIX) API (pipe, fork, dup2, execvp, read, write, sleep, close, readlink)
#include <string.h>     // 문자열 처리 함수 (strlen, strcmp, strerror, strrchr)
#include <sys/wait.h>   // 자식 프로세스의 종료를 기다리는 waitpid 함수
#include <errno.h>      // 시스템 에러 코드를 담고 있는 errno 변수
#include <fcntl.h>      // fcntl() 함수 사용 (파일 디스크립터 속성 제어)
#include <sys/time.h>   // timeval 구조체 사용 (select 타임아웃)
#include <sys/select.h> // select() 원형
#include "cJSON.h"      // cJSON 라이브러리 사용을 위한 헤더
#include "hardware.h"

// --- 2. 전역 변수 ---
static pid_t python_pid = -1;
static FILE* stream_to_python = NULL;
static FILE* stream_from_python = NULL;
static int pipe_from_python_fd = -1;

//요청할 PID와 완료 조건을 짝지어 목록으로 정의함
static const CANRequest pids_to_request[] ={
    {PID_ENGINE_SPEED, ENGINE_SPEED_FLAG},
    {PID_VEHICLE_SPEED, VEHICLE_SPEED_FLAG},
    {PID_GEAR_STATE, GEAR_STATE_FLAG},
    {PID_BRAKE_DATA, BRAKE_DATA_FLAG},
    {PID_TIRE_DATA, TIRE_DATA_FLAG}
};
//확인할 PID의 갯수
static const unsigned char num_pids_to_request = sizeof(pids_to_request) / sizeof(pids_to_request[0]);
//다음에 확인할 PID의 인덱스를 저장할 변수
static unsigned char next_pid_index = 0;

/* =======================================================================================
 * ===== [ADD] 헬퍼: 파이썬 analyze 요청 라인 프로토콜 전송 (GPS/STEER 포함) ==================
 *  - 목적: 필수 데이터(GPS, 스티어링)가 준비된 시점에 단 한 줄로 명령을 보냄.
 *  - 형식: C -> Py 로 "analyze {json}\n"
 *  - 주의: fflush( ) 필수 (라인버퍼링 보장)
 * ======================================================================================= */
static int send_ai_request(FILE* to_py, const VehicleData* v) {
    if (!to_py || !v) return -1;

    /* JSON에 부동소수점 수치를 넣음 */
    int n = fprintf(to_py,
                    "analyze {\"gps\":[%.6f,%.6f],\"steer\":%.2f}\n",
                    v->gps_x, v->gps_y, v->degree);
    if (n <= 0) return -1;

    /* 매우 중요: stdio 버퍼가 파이프로 실제 전달되도록 즉시 비움 */
    fflush(to_py);
    return 0;
}

/* =======================================================================================
 * ===== [ADD] 헬퍼: 파이썬 한 줄(JSON) 처리 ==============================================
 *  - 목적: Py -> C로 들어온 한 줄(JSON 문자열)을 파싱해 ai_result에 저장하고 상태 플래그 설정
 *  - 실패해도 치명적이지 않으므로 파싱 실패는 로깅 후 무시(프로토타입 전략)
 * ======================================================================================= */
static int handle_python_line(const char* line, unsigned char* state_flag, cJSON** out_ai) {
    if (!line || !state_flag) return -1;

    /* 파이썬 vision_server.py는 print(json.dumps(...)) + flush로 한 줄을 보낸다고 가정.
       JSON 포맷이 아닐 가능성(디버그 문자열 등)이 있으므로 파싱 실패는 에러로 두지 않는다. */
    cJSON* root = cJSON_Parse(line);
    if (!root) {
        /* 필요하면 fprintf(stderr, "..."); 로 남길 수 있음 */
        return -1;
    }
    if (*out_ai) cJSON_Delete(*out_ai);
    *out_ai = root;

    *state_flag |= AI_REQUEST_FLAG;   // "AI 결과 수신" 상태 완료
    return 0;
}

// --- 3. main 함수: 모든 코드의 시작점 ---
int main() {
    // --- 2-1. 파이프(Pipe) 생성 ---
    // 파이프는 OS 커널 내부에 생성되는 단방향 데이터 통로입니다.
    // [0]은 읽기 전용(출구), [1]은 쓰기 전용(입구) 파일 디스크립터(fd)입니다.
    int c_to_python_pipe[2];
    int python_to_c_pipe[2];
    if (pipe(c_to_python_pipe) == -1 || pipe(python_to_c_pipe) == -1) {
        perror("pipe() failed");
        exit(EXIT_FAILURE);
    }

    // --- 2-2. 자식 프로세스 생성 (fork) ---
    // fork()는 현재 프로세스를 그대로 복제하여 자식 프로세스를 만듭니다.
    // 부모에게는 자식의 PID(양수)를, 자식에게는 0을 반환합니다.
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        exit(EXIT_FAILURE);
    }

    // --- 3. 자식 프로세스(Python으로 변신할) 코드 영역 ---
    if (pid == 0) {
        // --- 3-1. 표준 입출력(I/O) 재지정 ---
        // '물길 바꾸기' 작업. 자식 프로세스의 기본 통신 채널을 우리가 만든 파이프로 연결합니다.
        // 이를 통해 Python은 복잡한 파이프 제어 없이, 평범한 input()/print()로 C와 통신할 수 있게 됩니다.
        dup2(c_to_python_pipe[0], STDIN_FILENO); // 표준 입력을 C->Py 파이프의 '출구'로 교체
        dup2(python_to_c_pipe[1], STDOUT_FILENO);// 표준 출력을 Py->C 파이프의 '입구'로 교체

        // --- 3-2. 불필요한 파이프 fd 닫기 ---
        // 자식은 이제 stdin/stdout이라는 더 큰 통로를 사용하므로, 원본 파이프 fd들은 모두 닫아줍니다.
        close(c_to_python_pipe[0]);
        close(c_to_python_pipe[1]);
        close(python_to_c_pipe[0]);
        close(python_to_c_pipe[1]);

        // --- 3-3. Python 스크립트의 절대 경로 동적 계산 ---
        // 어디에서 실행되는 파이썬 스크립트를 찾음
        char exe_path[1024];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            char *bin_dir = strrchr(exe_path, '/');
            if (bin_dir != NULL) *bin_dir = '\0';
            char *base_dir = strrchr(exe_path, '/');
            if (base_dir != NULL) *base_dir = '\0';
            char script_path[1024];
            snprintf(script_path, sizeof(script_path), "%s/ai/vision_server.py", exe_path);
            
            fprintf(stderr, "[C Child] Found python script at: %s\n", script_path);

            // --- 3-4. Python 스크립트 실행 (프로세스 변신) ---
            char *args[] = {"python3", script_path, NULL};
            execvp(args[0], args);
        }
        
        // execvp 또는 경로 계산 실패 시 아래 코드가 실행됨
        fprintf(stderr, "EXECVP or Path Calculation FAILED: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // --- 4. 부모 프로세스(C 지휘자) 코드 영역 ---
    else {
        // --- 4-1. 부모 프로세스의 파이프 및 스트림 설정 ---
        close(c_to_python_pipe[0]);
        close(python_to_c_pipe[1]);
        pipe_from_python_fd = python_to_c_pipe[0];
        stream_to_python = fdopen(c_to_python_pipe[1], "w");
        stream_from_python = fdopen(pipe_from_python_fd, "r");

        /* ===== I/O 버퍼링 정책 설정 ==========================================
         *  - to_python: 라인버퍼링으로 '\n' 마다 즉시 송신되게 함
         *  - from_python: 시스템 기본(버퍼링 상관없음, select로 가용 데이터 확인)
         * ========================================================================= */
        if (stream_to_python) setvbuf(stream_to_python, NULL, _IOLBF, 0);

        /* 파이썬 -> C 파이프 FD는 논블로킹으로: fgets/read가 데이터 없을 때 즉시 리턴 */
        fcntl(pipe_from_python_fd, F_SETFL, O_NONBLOCK);

        //CAN 버스 초기화
        int can_fd = can_init("can0");
        if(can_fd < 0){
            fprintf(stderr, "[C] FATAL: Failed to initialize CAN bus. Exiting.\n");
            exit(EXIT_FAILURE);
        }

        // --- 4-3. 상태 관리를 위한 변수 선언 ---
        VehicleData vehicle_data = {0}; // 차량 데이터를 저장할 구조체
        CANMessage can_message = {0};   // CAN통신 데이터 프레임
        cJSON* ai_result = NULL;        // AI 분석 결과를 저장할 JSON 객체
        unsigned char state_flag = 0;   // 상태 플래그
        unsigned char ai_state_flag = 0;// AI 분석 결과 플래그

        printf("[C] Main process start. Child PID: %d\n", pid);

        // --- 4-4. 메인 이벤트 루프: 장치의 심장 박동 ---
        while (1) {
             // --- A. 필수 데이터 수집 및 파이썬 요청 단계 ---
            // ai분석 요청을 하지 않았다면
            if((ai_state_flag & AI_REQUEST_FLAG) != AI_REQUEST_FLAG){
                //CAN 통신으로 필수 데이터를 받지 않았다면
                if((state_flag & AI_AVAILABLE) != AI_AVAILABLE){
                
                    //GPS 데이터를 받지 않았다면
                    if((state_flag & GPS_AVAILABLE) != GPS_AVAILABLE){
                        //X좌표 데이터를 받지 않았다면
                        if((state_flag & GPS_XDATA_FLAG) != GPS_XDATA_FLAG){
                            //X좌표 데이터 요청
                            if(can_request_pid(PID_GPS_XDATA) < 0){
                                perror("[C] PID_GPS_XDATA request error");
                            }
                        }
                        //x좌표 데이터를 받았다면
                        else{
                            //Y좌표 데이터 요청
                            if((state_flag & GPS_YDATA_FLAG) != GPS_YDATA_FLAG){   // ✅ FLAG로 검사
                                if(can_request_pid(PID_GPS_YDATA) < 0){
                                    perror("[C] PID_GPS_YDATA request error");
                                }
                            }
                        }
                    }
                    //GPS 데이터를 받았다면
                    else{
                        //스티어링 데이터 요청
                        if(can_request_pid(PID_STEERING_DATA) < 0){
                            perror("[C] STEERING_DATA request error");
                        }
                    }
                }

                //필수 데이터를 모두 수집했으면
                else{
                    //여기에 파이썬 실행 코드 추가, GPS좌표와 스티어링 데이터를 넘김
                    // ===== [ADD] 필수 두 데이터(GPS, 조향각)가 준비되면, 파이썬에 분석 명령 전송 =====
                    if (send_ai_request(stream_to_python, &vehicle_data) == 0) {
                        ai_state_flag |= AI_REQUEST_FLAG;   // 중복 요청 방지
                    } else {
                        perror("[C] send_ai_request failed");
                    }
                }
            }

            //ai 분석 요청을 했다면
            else{
                //나머지 CAN 통신 요청
                // (요구사항: AI가 돌고 있는 동안에도 CAN 수집은 계속된다)
                // 필요시 여기에서 엔진, 속도, 브레이크, 타이어 등 추가 PID를 라운드-로빈으로 요청해도 됨.
                // 예시(프로토타입): 속도/엔진RPM 라운드로 요청
                
                //다른 CAN 데이터를 받지 못핬다면
                if((state_flag & DATA_AVAILABLE) != DATA_AVAILABLE){

                    //리스트를 순회하며 아직 받지 못한 PID를 찾음
                    for(unsigned char i = 0; i < num_pids_to_request; i++){
                        //이번 순서에 확인할 요청 정보 가져오기
                        const CANRequest* current_req = &pids_to_request[next_pid_index];

                        //이 요청에 해당하는 데이터가 아직 수신되지 않았으면
                        if((state_flag & current_req->flag) != current_req->flag){
                            
                            //CAN버스로 데이터 요청
                            if(can_request_pid(current_req->pid) < 0){
                                ;;
                            }

                            //인덱스 업데이트
                            next_pid_index = (next_pid_index + 1) % num_pids_to_request;
                            break;
                        }
                        //이미 플래그가 세워져 있으면 인덱스 1 증가시킴
                        next_pid_index = (next_pid_index + 1) % num_pids_to_request;

                    }
                }
                

                
            }

            // --- B. I/O 멀티플렉싱(select) 단계 ---
            /* ===== [ADD] select()로 파이썬 파이프 + CAN 소켓을 동시에 감시 ==================
             *  - 타임아웃: 50ms (프로토타입 기준 반응성/부하 균형)
             *  - 준비된 FD만 비동기적으로 처리 → 한 루프에서 가능한 한 많이 소화
             * ========================================================================= */
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(pipe_from_python_fd, &rfds);
            FD_SET(can_fd, &rfds);
            int maxfd = (pipe_from_python_fd > can_fd ? pipe_from_python_fd : can_fd);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 50 * 1000; // 50 ms

            int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
            if (ready < 0) {
                if (errno == EINTR) continue; // 신호로 깨어남(무시)
                perror("[C] select");
                break;
            }
            if (ready == 0) {
                // 타임아웃: 주기 작업 자리(하트비트 등)
                // continue;
            }

            // >>> 1) 파이썬 결과 수신 (라인 단위 JSON)
            if (FD_ISSET(pipe_from_python_fd, &rfds)) {
                /* 주의: fd는 논블로킹. stream_from_python은 stdio 버퍼를 쓰므로
                   fgets가 즉시 NULL을 줄 수 있음(EAGAIN). 이는 '아직 한 줄이 안 채워짐' 의미 */
                char line[4096];
                while (fgets(line, sizeof(line), stream_from_python)) {
                    /* vision_server.py는 결과를 한 줄 JSON으로 print하고 flush함 */
                    handle_python_line(line, &state_flag, &ai_result);
                    /* 파이썬이 여러 줄을 연속적으로 보낼 수 있으므로 while로 드레인 */
                }

                /* EOF(파이썬 종료) 감지 */
                if (feof(stream_from_python)) {
                    fprintf(stderr, "[C] Python EOF detected. Exiting.\n");
                    break;
                }
                clearerr(stream_from_python); // EAGAIN 등 클리어
            }

            // >>> 2) CAN 프레임 수신 (있을 때 모두 드레인)
            if (FD_ISSET(can_fd, &rfds)) {
                while (1) {
                    int r = can_receive_message(&can_message);
                    if (r < 0) {
                        // 심각한 소켓 에러 가능 (프로토타입: 경고만)
                        perror("[C] can_receive_message");
                        break;
                    } else if (r == 0) {
                        // 읽을 데이터 없음(논블로킹): 루프 종료
                        break;
                    } else {
                        // 정상 프레임 1개 수신 → 파싱 및 상태 플래그 갱신
                        can_parse_and_update_data(&can_message, &vehicle_data, &state_flag);
                    }
                }
            }

            // >>> 3) 완료 조건 체크: AI 결과 + CAN 측 “완료 세트” 충족 시 제어 로직 실행
            /* COMPLETE_DATA_FLAG는 hardware.h에 정의된 전체 데이터 집합 플래그임.
               (ENGINE_SPEED, VEHICLE_SPEED, GEAR_STATE, GPS, STEERING, BRAKE, TIRE 등)
               프로토타입에서는 이 완전 세트를 만족했을 때 한 번 제어 로직을 실행하도록 구성. */
            if ( ((ai_state_flag & 0x01) == 0x01) &&
                 ((state_flag & COMPLETE_DATA_FLAG) == COMPLETE_DATA_FLAG) ) {

                // ======= [ADD] 최종 제어 로직 트리거 지점 ===================================
                // 예) control_decision(&vehicle_data, ai_result);
                //     - ai_result에서 위험도/객체/권고조향/감속명령 등을 추출
                //     - vehicle_data(실측 CAN)와 결합해 실행 정책 판단
                // ===========================================================================
                fprintf(stdout,
                        "[C] CONTROL: AI+CAN 완료. speed=%d rpm=%d gear=%c gps(%.6f,%.6f) steer=%.3f\n",
                        vehicle_data.speed, vehicle_data.rpm, vehicle_data.gear_state,
                        vehicle_data.gps_x, vehicle_data.gps_y, vehicle_data.degree);
                fflush(stdout);

                // (정책) 다음 사이클 준비: AI 관련 플래그/결과만 리셋 → CAN은 계속 최신 값 유지
                state_flag &= ~(GPS_XDATA_FLAG | GPS_YDATA_FLAG | STEERING_DATA_FLAG);
                ai_state_flag = 0;
                if (ai_result) { cJSON_Delete(ai_result); ai_result = NULL; }

                // 필요하면 필수 두 데이터(GPS/STEER) 플래그만 리셋해서,
                // 다시 두 데이터를 확보한 뒤 새로운 analyze 라운드를 도는 정책도 가능.
                // 예)
            }

        } // --- while(1) 루프 끝 ---

        // --- 자원 정리 및 종료 ---
        printf("\n[C] Main process finished. Cleaning up resources.\n");
        if (stream_to_python) fclose(stream_to_python);
        if (stream_from_python) fclose(stream_from_python);
        if (pid > 0) waitpid(pid, NULL, 0); // 좀비 프로세스 방지
    }
    return 0;
}
