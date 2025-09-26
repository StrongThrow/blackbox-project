# ================= libhardware Makefile (C + C++) =================
CC      ?= gcc
CXX     ?= g++
CFLAGS   = -Wall -O2 -fPIC -I./include
CXXFLAGS = $(CFLAGS)
LDFLAGS  = -shared
LDLIBS  ?=

# (옵션) RealSense 사용 시 pkg-config로 플래그 추가
REALSENSE_CFLAGS := $(shell pkg-config --cflags librealsense2 2>/dev/null)
REALSENSE_LIBS   := $(shell pkg-config --libs   librealsense2 2>/dev/null)
CJSON_CFLAGS := $(shell pkg-config --cflags cjson 2>/dev/null)
CJSON_LIBS   := $(shell pkg-config --libs   cjson 2>/dev/null)
GST_CFLAGS := $(shell pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 2>/dev/null)
GST_LIBS   := $(shell pkg-config --libs   gstreamer-1.0 gstreamer-app-1.0 2>/dev/null)
CFLAGS   += $(GST_CFLAGS)
CXXFLAGS += $(GST_CFLAGS)
LDLIBS   += $(GST_LIBS)
CFLAGS   += $(CJSON_CFLAGS)
CXXFLAGS += $(CJSON_CFLAGS)
LDLIBS   += $(CJSON_LIBS)
CXXFLAGS += $(REALSENSE_CFLAGS)
LDLIBS   += $(REALSENSE_LIBS)

SRC_DIR   = src
BUILD_DIR = ../build
TARGET    = $(BUILD_DIR)/lib/libhardware.so

C_SRCS    = $(wildcard $(SRC_DIR)/*.c)
CPP_SRCS  = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS   = $(patsubst $(SRC_DIR)/%.c,  $(BUILD_DIR)/obj/%.o, $(C_SRCS)) \
            $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/obj/%.o, $(CPP_SRCS))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo "Linking: $@"
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)   # C++ 오브젝트 있으면 CXX로 링크

$(BUILD_DIR)/obj/%.o: $(SRC_DIR)/%.c
	@echo "CC  $<"
	@mkdir -p $(@D)
	$(CC)  $(CFLAGS)   -c -o $@ $<

$(BUILD_DIR)/obj/%.o: $(SRC_DIR)/%.cpp
	@echo "CXX $<"
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	@echo "Cleaning..."
	rm -rf $(BUILD_DIR)

.PHONY: all clean

