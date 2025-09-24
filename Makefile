# =================================================================
#           Blackbox Project - 최상위 Makefile
# =================================================================

# make의 기본 동작을 정의합니다.
# .PHONY는 해당 이름의 파일이 있더라도 명령을 실행하도록 보장합니다.
.PHONY: all lib app run clean

# 'make' 또는 'make all' 실행 시 기본적으로 수행할 목표
# 라이브러리(lib)를 먼저 빌드하고, 그 다음에 애플리케이션(app)을 빌드합니다.
all: lib app

# 'libhardware' 라이브러리를 빌드하는 규칙
lib:
	@echo "--- Building Hardware Library (libhardware.so) ---"
	$(MAKE) -C libhardware

# 'blackbox_main' 애플리케이션을 빌드하는 규칙
# 라이브러리가 먼저 빌드되어야 하므로 'lib'에 의존합니다.
app: lib
	@echo "--- Building Main Application (blackbox_main) ---"
	$(MAKE) -C app

# 컴파일된 프로그램을 실행하는 규칙
run: all
	@echo "--- Running The Application ---"
	# build/lib 경로에 있는 공유 라이브러리를 찾을 수 있도록 환경 변수 설정 후 실행
	export LD_LIBRARY_PATH=./build/lib && ./build/bin/blackbox_main

# 모든 빌드 결과물을 삭제하는 규칙
clean:
	@echo "--- Cleaning up the project ---"
	$(MAKE) -C libhardware clean
	$(MAKE) -C app clean
	# 최상위 build 폴더도 삭제할 경우 아래 라인 추가
	# rm -rf build