#!/bin/bash

# 이 스크립트 파일이 위치한 디렉터리를 기준으로 경로를 설정합니다.
# 이렇게 하면 어떤 위치에서든 이 스크립트를 실행해도 문제없이 동작합니다.
SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

echo "--- Blackbox Application Starting ---"

# 1. 공유 라이브러리 경로 설정
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib"
echo "Library path set to: ${LD_LIBRARY_PATH}"

# 2. 메인 애플리케이션 실행
"${SCRIPT_DIR}/bin/blackbox_main"

echo "--- Blackbox Application Finished ---"