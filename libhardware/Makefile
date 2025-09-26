# =================================================================
#    libhardware Makefile (cJSON Submodule + System GStreamer)
# =================================================================
CC      ?= gcc
CFLAGS   = -Wall -O2 -fPIC -I./include -I../vendor/cJSON
LDFLAGS  = -shared
LDLIBS  ?=

# --- GStreamer 라이브러리 의존성 설정 (조건부) ---
# CROSS_COMPILE 변수가 비어있을 때 (네이티브 빌드 시)만 pkg-config 사용
ifeq ($(strip $(CROSS_COMPILE)),)
    GST_CFLAGS := $(shell pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 2>/dev/null)
    GST_LIBS   := $(shell pkg-config --libs   gstreamer-1.0 gstreamer-app-1.0 2>/dev/null)
    CFLAGS += $(GST_CFLAGS)
    LDLIBS += $(GST_LIBS)
else
# CROSS_COMPILE 변수가 있을 때 (크로스 컴파일 시)는 라이브러리 이름만 직접 링크
    LDLIBS += -lgstreamer-1.0 -lgstapp-1.0
endif

# --- 소스 및 결과물 경로 정의 ---
SRC_DIR   = src
BUILD_DIR = ../build
TARGET    = $(BUILD_DIR)/lib/libhardware.so

# C_SRCS: 우리 소스 코드와 cJSON 소스 코드를 모두 포함
C_SRCS    = $(wildcard $(SRC_DIR)/*.c) ../vendor/cJSON/cJSON.c
OBJECTS   = $(patsubst %.c, $(BUILD_DIR)/obj/c/%.o, $(C_SRCS))

all: $(TARGET)

# 최종 라이브러리 링크
$(TARGET): $(OBJECTS)
	@echo "Linking Library: $@"
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# .c 파일을 .o 파일로 컴파일하는 규칙
$(BUILD_DIR)/obj/c/%.o: %.c
	@echo "Compiling C: $<"
	@mkdir -p $(@D)
	$(CC)  $(CFLAGS) -c -o $@ $<

clean:
	@echo "Cleaning up library build files..."
	rm -rf $(BUILD_DIR)/obj $(BUILD_DIR)/lib

.PHONY: all clean