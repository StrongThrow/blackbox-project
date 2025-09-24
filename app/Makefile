# =================================================================
#               App (blackbox_main) 빌드용 Makefile
# =================================================================

# --- 컴파일러 및 옵션 정의 ---
CC = gcc
# CFLAGS: 컴파일 옵션
# -Wall: 모든 경고 메시지 출력
# -I../libhardware/include: ../libhardware/include 경로에서 헤더 파일(hardware.h)을 찾음
CFLAGS = -Wall -I../libhardware/include
# LDFLAGS: 링커 옵션
# -L../build/lib: ../build/lib 경로에서 라이브러리 파일(lib*.so)을 찾음
# -lhardware: libhardware.so 라이브러리와 링크
# -lcjson: libcjson.so 라이브러리와 링크 (Yocto OS에 설치되어 있어야 함)
LDFLAGS = -L../build/lib -lhardware -lcjson

# --- 소스 및 결과물 경로 정의 ---
# SRC: 컴파일할 소스 파일
SRC = src/main.c
# BUILD_DIR: 최종 결과물이 저장될 위치
BUILD_DIR = ../build
# TARGET: 최종적으로 생성될 실행 파일의 전체 경로
TARGET = $(BUILD_DIR)/bin/blackbox_main


# --- 빌드 규칙 정의 ---

# 'make' 또는 'make all' 실행 시 기본적으로 수행할 목표
all: $(TARGET)

# 최종 실행 파일($(TARGET))을 생성하는 규칙
# $@: 목표 파일 이름 (e.g., ../build/bin/blackbox_main)
# $<: 첫 번째 의존성 파일 이름 (e.g., src/main.c)
$(TARGET): $(SRC)
	@echo "Compiling and linking application: $@"
	# 빌드 결과물이 저장될 디렉터리가 없으면 생성
	@mkdir -p $(@D)
	# 소스 파일을 컴파일하고, 지정된 라이브러리들과 링크하여 최종 실행 파일을 생성
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# 'make clean' 실행 시 컴파일 결과물을 삭제하는 규칙
clean:
	@echo "Cleaning up application build files..."
	rm -f $(TARGET)

# 가상 목표(PHONY) 선언
.PHONY: all clean