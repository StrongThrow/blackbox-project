#!/bin/bash


SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

echo "--- Blackbox Application Starting ---"

# 1. 공유 라이브러리 경로 설정
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib"
echo "Library path set to: ${LD_LIBRARY_PATH}"

# 2. 메인 애플리케이션 실행
"${SCRIPT_DIR}/bin/blackbox_main"

echo "--- Blackbox Application Finished ---"