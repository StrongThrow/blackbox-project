# =================================================================
#           Blackbox Project - 최상위 Makefile (최종)
# =================================================================

# --- 배포 기본값 (필요시 커맨드라인에서 덮어쓰기) ---
PI_USER ?= pi
PI_IP   ?= 10.10.14.61

# --- 기본 툴체인 (환경에서 CC/CXX/PKG_CONFIG가 오면 그걸 사용) ---
CC         ?= gcc
CXX        ?= g++
PKG_CONFIG ?= pkg-config

# --- CROSS_COMPILE 프리셋 (Yocto SDK가 없을 때만 사용) ---
CROSS_COMPILE ?= aarch64-linux-gnu-

.PHONY: all cross lib app run deploy clean

# 네이티브 빌드 (호스트 테스트용)
all: lib app

# 크로스 빌드
# - Yocto SDK를 로드한 경우(OECORE_TARGET_SYSROOT 존재)엔 SDK 설정을 그대로 사용
# - 아니면 CROSS_COMPILE 프리셋로 CC/CXX 지정
cross:
	@echo "--- Cross-compiling for Raspberry Pi (ARM64) ---"
	@if [ -n "$$OECORE_TARGET_SYSROOT" ]; then \
		echo "[Yocto SDK detected] CC=$(CC) CXX=$(CXX) PKG_CONFIG=$(PKG_CONFIG)"; \
		$(MAKE) all CC="$(CC)" CXX="$(CXX)" PKG_CONFIG="$(PKG_CONFIG)"; \
	else \
		echo "[Using CROSS_COMPILE=$(CROSS_COMPILE)]"; \
		$(MAKE) all CC="$(CROSS_COMPILE)gcc" CXX="$(CROSS_COMPILE)g++" PKG_CONFIG="$(PKG_CONFIG)"; \
	fi

# 라이브러리 / 앱
lib:
	$(MAKE) -C libhardware CC="$(CC)" CXX="$(CXX)" PKG_CONFIG="$(PKG_CONFIG)"

app:
	$(MAKE) -C app CC="$(CC)" CXX="$(CXX)" PKG_CONFIG="$(PKG_CONFIG)"

# 배포 (크로스 빌드 후)
deploy: cross
	@echo "--- Deploying to Raspberry Pi $(PI_USER)@$(PI_IP) ---"
	./deploy.sh $(PI_USER) $(PI_IP)

# 실행 (타깃에서)
run:
	@echo "This target should be run on the Raspberry Pi."

# 정리
clean:
	@echo "--- Cleaning up the project ---"
	$(MAKE) -C libhardware clean
	$(MAKE) -C app clean
	rm -rf build
