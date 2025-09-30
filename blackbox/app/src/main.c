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
#include "cJSON.h"      // cJSON 라이브러리 사용을 위한 헤더
#include "hardware.h"

// --- 2. main 함수: 모든 코드의 시작점 ---
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
        // "어디서 실행되든 Python 스크립트를 찾아라!"
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
        FILE* stream_to_python = fdopen(c_to_python_pipe[1], "w");
        FILE* stream_from_python = fdopen(python_to_c_pipe[0], "r");

        // --- 4-2. 비동기 I/O를 위한 파일 디스크립터(fd) 설정 ---
        int pipe_fd = python_to_c_pipe[0];
        //CAN 버스 초기화
        int can_fd = can_init("can0");
        if(can_fd < 0){
            fprintf(stderr, "[C] FATAL: Failed to initialize CAN bus. Exiting.\n");
            exit(EXIT_FAILURE);
        }

        // [중요] select()를 위해 감시할 fd들을 논블로킹(Non-blocking) 모드로 설정합니다.
        fcntl(pipe_fd, F_SETFL, O_NONBLOCK);
        // if (can_fd != -1) fcntl(can_fd, F_SETFL, O_NONBLOCK);

        // --- 4-3. 상태 관리(State Management)를 위한 변수 선언 ---
        // 비동기적으로 도착하는 데이터들을 저장하고, 수신 여부를 기록하는 '상태 변수'들입니다.
        cJSON* latest_ai_result = NULL;
        int ai_result_received = 0;   // AI 결과 수신 깃발
        int can_frame_received = 0;   // CAN 메시지 수신 깃발
        int analysis_requested = 0;   // 현재 분석 요청이 진행 중인지 여부 깃발

        printf("[C] Main process started in ASYNC mode. Child PID: %d\n", pid);

        // --- 4-4. 메인 이벤트 루프: 장치의 심장 박동 ---
        while (1) {
            // --- AI 분석 요청 단계 ---
            // 이전 사이클이 완료되었을 때만 새로운 분석 요청을 보냅니다. (중복 요청 방지)
            if (!analysis_requested) {
                printf("\n[C] Sending 'analyze' command to Python.\n");
                fprintf(stream_to_python, "analyze\n");
                fflush(stream_to_python); // [중요] fflush: "지금 바로 보내!" 라는 명령
                analysis_requested = 1;
            }

            // --- I/O 멀티플렉싱(select) 단계: "AI 결과나 CAN 메시지 중 뭐라도 오면 깨워줘" ---
            fd_set read_fds;
            FD_ZERO(&read_fds);                 // 감시 목록 초기화
            FD_SET(pipe_fd, &read_fds);         // Python 파이프를 감시 목록에 추가
            FD_SET(can_fd, &read_fds);          // CAN 통신 파이프를 감시 목록에 추가

            //1. 두 fd중 더 큰 값을 찾아서 max_fd에 저장
            int max_fd = (pipe_fd > can_fd) ? pipe_fd : can_fd;
            struct timeval timeout;
            timeout.tv_sec = 1; timeout.tv_usec = 0; // 1초간 응답 없으면 타임아웃
            
            // select() 함수에 "가장 큰 번호 + 1"을 전달
            int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

            if (activity < 0) { perror("select() error"); break; }

            // --- 이벤트 처리 단계: "누가 연락했는지 확인하고 데이터 저장하기" ---
            // [ Python으로부터 AI 결과가 도착했는지 확인 ]
            if (FD_ISSET(pipe_fd, &read_fds)) {
                char buffer[2048];
                if (fgets(buffer, sizeof(buffer), stream_from_python) != NULL) {
                    printf("[C] Event: AI result ARRIVED.\n");
                    if (latest_ai_result != NULL) cJSON_Delete(latest_ai_result);
                    latest_ai_result = cJSON_Parse(buffer);
                    ai_result_received = 1; // AI 결과 수신 깃발 올리기
                }
            }
            // [ CAN 버스에서 메시지가 도착했는지 확인 ]
            if (FD_ISSET(can_fd, &read_fds)){
                int result = can_receive_message(&lastest_can_frame);
            }
            
            // [!!!] 테스트를 위해, 분석 요청이 나간 상태라면 CAN 메시지는 항상 받았다고 시뮬레이션
            if (analysis_requested) {
                 can_frame_received = 1; // CAN 메시지 수신 깃발 올리기
            }

            // --- 최종 결정 및 제어 단계: "모든 정보가 모였으니, 행동 개시!" ---
            if (ai_result_received && can_frame_received) {
                printf("[C] Condition Met: Both AI result and CAN frame are ready!\n");
                
                if (latest_ai_result != NULL) {
                    printf("[C] Making final decision and controlling hardware...\n");
                    // [ 여기에 latest_ai_result와 latest_can_frame을 조합하여 최종 제어하는 코드 삽입 ]
                }
                
                // 한 사이클이 끝났으므로, 다음 사이클을 위해 모든 상태 변수를 초기화합니다.
                printf("[C] Resetting state for next cycle.\n");
                if (latest_ai_result != NULL) {
                    cJSON_Delete(latest_ai_result);
                    latest_ai_result = NULL;
                }
                ai_result_received = 0;
                can_frame_received = 0;
                analysis_requested = 0;
            }
        } // --- while(1) 루프 끝 ---

        // --- 자원 정리 및 종료 ---
        printf("\n[C] Main process finished. Cleaning up resources.\n");
        fclose(stream_to_python);
        fclose(stream_from_python);
        if (pid > 0) waitpid(pid, NULL, 0); // 좀비 프로세스 방지
    }
    return 0;
}

//기환 테스트, git branch
