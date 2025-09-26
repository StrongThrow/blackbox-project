# =================================================================
#               App (blackbox_main) 빌드용 Makefile (최종본)
# =================================================================

# --- 컴파일러 및 옵션 정의 ---
CC ?= gcc
# CFLAGS: libhardware 및 cJSON 헤더 파일 경로 추가
CFLAGS = -Wall -I../libhardware/include -I../vendor/cJSON
# LDFLAGS: libhardware 라이브러리가 있는 경로(-L)와 라이브러리 이름(-l) 지정
LDFLAGS = -L../build/lib -lhardware

# --- 소스 및 결과물 경로 정의 ---
SRC = src/main.c
BUILD_DIR = ../build
TARGET = $(BUILD_DIR)/bin/blackbox_main
# 의존해야 할 라이브러리 파일을 명시적으로 지정
LIB_DEPENDENCY = $(BUILD_DIR)/lib/libhardware.so

# --- 빌드 규칙 정의 ---
all: $(TARGET)

# 최종 실행 파일($(TARGET))을 생성하는 규칙
# 의존성에 $(LIB_DEPENDENCY)를 추가하여, 라이브러리가 먼저 생성되었는지 확인
$(TARGET): $(SRC) $(LIB_DEPENDENCY)
	@echo "Compiling and linking application..."
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	@echo "Cleaning up application build files..."
	rm -f $(TARGET)

.PHONY: all clean