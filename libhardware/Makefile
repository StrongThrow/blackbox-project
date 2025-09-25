# =================================================================
#               libhardware 라이브러리 빌드용 Makefile
# =================================================================

# --- 컴파일러 및 옵션 정의 ---
CC ?= gcc
# CFLAGS: 컴파일 옵션
# -Wall: 모든 경고 메시지를 출력 (코드 품질 향상)
# -fPIC: 위치 독립 코드 생성 (공유 라이브러리 필수 옵션)
# -I./include: 'hardware.h' 헤더 파일을 찾기 위해 ./include 디렉터리를 포함
CFLAGS = -Wall -fPIC -I./include
# LDFLAGS: 링커 옵션
# -shared: 공유 라이브러리를 생성하도록 지시
LDFLAGS = -shared

# --- 소스 및 결과물 경로 정의 ---
# SRC_DIR: 소스 파일이 있는 디렉터리
SRC_DIR = src
# BUILD_DIR: 컴파일된 중간 파일(.o) 및 최종 결과물(.so)이 저장될 위치
BUILD_DIR = ../build
# TARGET: 최종적으로 생성될 라이브러리 파일의 전체 경로
TARGET = $(BUILD_DIR)/lib/libhardware.so

# --- 소스 파일 자동 탐색 ---
# SOURCES: src 디렉터리 안의 모든 .c 파일 목록을 자동으로 찾음
#          (예: src/camera.c src/lcd.c ...)
SOURCES = $(wildcard $(SRC_DIR)/*.c)
# OBJECTS: 각 .c 파일에 해당하는 .o (오브젝트) 파일 경로 목록을 자동으로 생성
#          (예: ../build/obj/src/camera.o ../build/obj/src/lcd.o ...)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/obj/%.o, $(SOURCES))


# --- 빌드 규칙 정의 ---

# 'make' 또는 'make all' 실행 시 기본적으로 수행할 목표
all: $(TARGET)

# 최종 라이브러리 파일($(TARGET))을 생성하는 규칙
$(TARGET): $(OBJECTS)
	@echo "Linking library: $@"
	# 빌드 결과물이 저장될 디렉터리가 없으면 생성
	@mkdir -p $(@D)
	# 모든 오브젝트 파일(.o)들을 묶어 공유 라이브러리(.so)를 생성
	$(CC) $(LDFLAGS) -o $@ $^

# 각 소스 파일(.c)을 오브젝트 파일(.o)로 컴파일하는 패턴 규칙
$(BUILD_DIR)/obj/%.o: $(SRC_DIR)/%.c
	@echo "Compiling: $< -> $@"
	@mkdir -p $(@D)
	# .c 파일을 컴파일하여 .o 파일을 생성 (-c 옵션: 링크는 하지 않음)
	$(CC) $(CFLAGS) -c -o $@ $<

# 'make clean' 실행 시 컴파일 결과물을 삭제하는 규칙
clean:
	@echo "Cleaning up build files..."
	rm -rf $(BUILD_DIR)

# 가상 목표(PHONY) 선언
.PHONY: all clean