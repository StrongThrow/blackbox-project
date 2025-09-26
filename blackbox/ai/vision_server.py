#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
길이 프레이밍 기반(Mock) AI 서버
- 입력: [int32_le width][int32_le height][RGB888 raw width*height*3]
- 출력: [uint32_le json_len][json_bytes]
- 로그는 stderr로만 출력 (stdout은 오직 바이너리 프로토콜)
"""

import sys
import time
import json
import struct

def log(msg: str):
    sys.stderr.write(msg + "\n")
    sys.stderr.flush()

def read_exact(n: int) -> bytes:
    """stdin에서 정확히 n바이트를 읽음. EOF면 프로세스 종료."""
    buf = bytearray()
    rd = sys.stdin.buffer.read
    while len(buf) < n:
        chunk = rd(n - len(buf))
        if not chunk:
            # EOF
            sys.exit(0)
        buf.extend(chunk)
    return bytes(buf)

def write_json(obj):
    """[4바이트 길이(LE)] + [JSON 바이트]"""
    b = json.dumps(obj, separators=(',', ':')).encode('utf-8')
    sys.stdout.buffer.write(struct.pack('<I', len(b)))
    sys.stdout.buffer.write(b)
    sys.stdout.buffer.flush()

def main():
    # 기동 대기 (카메라/부모 초기화 대기용)
    time.sleep(0.5)
    log("[MOCK AI] > AI 서버 준비 완료. 요청 대기 중...")

    while True:
        try:
            # ---- 1) 헤더 8바이트 수신 ----
            header = read_exact(8)
            # '<ii' = little-endian int32, int32
            width, height = struct.unpack('<ii', header)

            # ---- 2) 이미지 바디 수신 ----
            image_size = width * height * 3  # RGB888
            image_data = read_exact(image_size)

            # (여기에서 실제 추론 수행하면 됨. 지금은 mock)
            log(f"[MOCK AI] > {width}x{height} 이미지 수신 ({image_size} bytes)")

            # ---- 3) 모의 추론 결과 작성 ----
            time.sleep(0.08)
            mock_result = {
                "detections": [
                    {"class_name": "person", "confidence": 0.92, "box": [120, 240, 50, 80]},
                    {"class_name": "car",    "confidence": 0.85, "box": [450, 260, 120, 150]},
                ]
            }

            # ---- 4) 길이 프레이밍으로 전송 ----
            write_json(mock_result)

        except BrokenPipeError:
            # 부모가 종료되면 파이프 끊김
            break
        except Exception as e:
            log(f"[MOCK AI] > 오류 발생: {e!r}")
            # 상황에 따라 계속 루프 돌 수도 있지만, 여기서는 종료
            break

if __name__ == '__main__':
    main()
