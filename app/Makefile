# =================================================================
#               App (blackbox_main) 빌드용 Makefile
#               (Git Submodule 'cJSON' 사용 버전)
# =================================================================

# --- 컴파일러 및 옵션 정의 ---
CC = gcc
# CFLAGS: cJSON 헤더 파일 경로(-I../vendor/cJSON) 추가
CFLAGS = -Wall -I../libhardware/include -I../vendor/cJSON
# LDFLAGS: 시스템 라이브러리 링크(-lcjson) 제거
LDFLAGS = -L../build/lib -lhardware

# --- 소스 및 결과물 경로 정의 ---
# SRC: main.c와 cJSON.c 소스 파일을 모두 포함
SRC = src/main.c ../vendor/cJSON/cJSON.c
# BUILD_DIR: 변경 없음
BUILD_DIR = ../build
# TARGET: 변경 없음
TARGET = $(BUILD_DIR)/bin/blackbox_main


# --- 빌드 규칙 정의 ---

# 'make' 또는 'make all' 실행 시 기본적으로 수행할 목표
all: $(TARGET)

# 최종 실행 파일($(TARGET))을 생성하는 규칙
# $^: 모든 의존성 파일 목록 (e.g., src/main.c ../vendor/cJSON/cJSON.c)
$(TARGET): $(SRC)
	@echo "Compiling and linking application with local cJSON..."
	@mkdir -p $(@D)
	# 소스 파일 목록($^)을 사용하여 컴파일 및 링크
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 'make clean' 실행 시 컴파일 결과물을 삭제하는 규칙
clean:
	@echo "Cleaning up application build files..."
	rm -f $(TARGET)

# 가상 목표(PHONY) 선언
.PHONY: all clean