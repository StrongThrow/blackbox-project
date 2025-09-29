# -*- coding: utf-8 -*-
"""
vision_server.py: [최종본] C 프로세스의 요청을 받아 GStreamer로부터 1프레임을 가져와 분석 결과를 반환하는 스크립트.
- 역할: C의 요청에 따라 AI 분석 서비스만 제공하는 '전문가'.
- 동작: 평소에는 C의 명령을 기다리며 대기(休). 명령을 받으면 즉시 임무(분석)를 수행하고 보고(결과 전송).
"""

import sys
import json
import gi
import numpy as np
# LDH gi클래스 구현을 위한 import 시작
import gi, numpy as np, time, cv2
import threading
from gi.repository import Gst, GstApp
# LDH gi클래스 구현을 위한 import 끝




# GStreamer 및 GObject 라이브러리 로드.
gi.require_version('Gst', '1.0')
gi.require_version('GstApp', '1.0')     # 추가 
gi.require_version('GstVideo', '1.0')   # 추가

# LDH GstVideoReceiver class 선언 시작
class GstVideoReceiver:
    def __init__(self, port):
        self.port = port
        self.pipeline = None
        self.appsink = None
        self.is_initialized = False
        self.latest_frame = None
        self._stop = False
        self._thread = None

    def init_pipeline(self):
        pipeline_str = (
            f"udpsrc port={self.port} ! "
            "application/x-rtp,media=video,encoding-name=H264,payload=96,clock-rate=90000 ! "
            "rtpjitterbuffer latency=60 ! "
            "rtph264depay ! h264parse config-interval=-1 ! "
            "avdec_h264 max-threads=1 ! "
            "videoconvert n-threads=1 ! "
            f"video/x-raw,format=BGR,width={WIDTH},height={HEIGHT} ! "
            "queue max-size-buffers=2 max-size-time=0 max-size-bytes=0 leaky=downstream ! "
            "appsink name=sink drop=true max-buffers=1 sync=false"
        )

        self.pipeline = Gst.parse_launch(pipeline_str)
        self.appsink = self.pipeline.get_by_name("sink")
        self.pipeline.set_state(Gst.State.PLAYING)
        self.is_initialized = True
        return True

    def _pull_frame(self):
        sample = self.appsink.try_pull_sample(50_000)  # 50ms
        if not sample:
            return None
        buf = sample.get_buffer()
        caps = sample.get_caps()
        s = caps.get_structure(0)
        w, h = int(s.get_value("width")), int(s.get_value("height"))
        data = buf.extract_dup(0, buf.get_size())
        if len(data) < w*h*3:
            return None
        return np.frombuffer(data, dtype=np.uint8, count=w*h*3).reshape(h, w, 3)

    def release(self):
        if self.is_initialized and self.pipeline:
            self.pipeline.set_state(Gst.State.NULL)
            self.is_initialized = False

    def _loop(self):
        while not self._stop:
            frame = self._pull_frame()
            if frame is not None:
                self.latest_frame = frame

    def start(self):
        self._stop = False
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop = True
        if self._thread:
            self._thread.join()
        if self.is_initialized and self.pipeline:
            self.pipeline.set_state(Gst.State.NULL)
# LDH GstVideoReceiver class 선언 끝

def log(message):
    """디버깅 메시지를 표준 에러(stderr)로 출력하는 함수. C로 가는 데이터(stdout)와 섞이지 않게 함."""
    print(f"[Py LOG] {message}", file=sys.stderr, flush=True)

def get_single_frame(appsink):
    """
    appsink로부터 프레임(샘플)을 단 하나만 가져와 NumPy 배열로 반환하는 함수.
    """
    # "pull-sample" 시그널은 appsink의 내부 버퍼에서 샘플을 꺼내오는 역할을 합니다.
    sample = appsink.emit("pull-sample")
    if not sample:
        log("Failed to pull sample from appsink. Is the stream running?")
        return None

    # 샘플에서 버퍼(실제 데이터)와 캡슐(데이터 형식 정보)을 추출합니다.
    buf = sample.get_buffer()
    caps = sample.get_caps()
    structure = caps.get_structure(0)
    height = structure.get_value("height")
    width = structure.get_value("width")
    
    # GStreamer 버퍼 메모리를 Python이 읽을 수 있도록 매핑합니다.
    result, mapinfo = buf.map(Gst.MapFlags.READ)
    if result:
        # 매핑된 메모리 데이터를 NumPy 배열로 변환합니다.
        numpy_frame = np.frombuffer(mapinfo.data, dtype=np.uint8).reshape(height, width, -1)
        buf.unmap(mapinfo) # 메모리 매핑 해제
        return numpy_frame
    return None

def main():
    """메인 실행 함수"""
    log("On-demand analysis script started.")
    
    # --- 1. GStreamer 파이프라인 설정 ---
    Gst.init(None)
    # [appsink 설정이 핵심]
    # - max-buffers=1: 프레임을 최대 1개만 저장. (메모리 절약)
    # - drop=true: 버퍼가 꽉 찼을 때 새 프레임이 오면, '헌 프레임은 버리고' 새 것으로 교체.
    # -> 이 두 설정 덕분에 C의 요청이 뜸해도 데이터가 쌓이지 않고, 항상 '가장 최신 프레임'만 유지됩니다.
    # pipeline_str = (
    #     "udpsrc port=5000 ! "
    #     "application/x-rtp, media=video, clock-rate=90000, encoding-name=H264, payload=96 ! "
    #     "rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! "
    #     "video/x-raw,format=BGR ! appsink name=sink max-buffers=1 drop=true"
    # )
    # pipeline = Gst.parse_launch(pipeline_str)
    # appsink = pipeline.get_by_name('sink')
    # pipeline.set_state(Gst.State.PLAYING)

    # LDH pipeline을 통해 이미지 받는 객체 생성 시작 
    receivers = [GstVideoReceiver(5000+i) for i in range(6)]
    for r in receivers:
        r.init_pipeline()
        r.start()
    # LDH pipeline을 통해 이미지 받는 객체 생성 끝

    log("GStreamer pipeline is running. Waiting for commands from C via stdin.")

    try:
        # --- 2. C로부터의 명령을 기다리는 메인 루프 ---
        while True:
            # sys.stdin.readline()은 C에서 명령을 보낼 때까지 여기서 실행을 멈추고 대기합니다.
            # 이것이 이 스크립트의 기본 '대기 상태'입니다.
            command = sys.stdin.readline()
            if not command:
                log("C process closed the pipe. Exiting.")
                break
            
            log(f"Received command from C: {command.strip()}")
            if "analyze" in command.strip():
                #frame = get_single_frame(appsink)
                frames = [] #이미지 넣을 리스트

                for r in receivers:
                    frame = r.latest_frame
                    if frame is None:
                        frame = np.zeros((HEIGHT, WIDTH, 3), dtype=np.uint8)
                    frames.append(frame)

                if frames is not None:
                    # [ 여기에 실제 AI 모델 추론 코드를 삽입합니다. ]
                    ai_result = {"status": "success", "frame_shape": frames.shape}
                    log(f"Frame analysis complete. Shape: {frames.shape}")
                else:
                    ai_result = {"status": "fail", "reason": "Could not get frame from GStreamer"}
                    log("Frame analysis failed.")
                
                # 분석 결과를 JSON 문자열로 변환하여 표준 출력(stdout)으로 보냅니다.
                # 이 stdout은 C의 파이프와 연결되어 있습니다.
                print(json.dumps(ai_result))
                # [중요] 버퍼를 비워 데이터가 즉시 C로 전송되게 합니다.
                sys.stdout.flush()

    except KeyboardInterrupt:
        log("KeyboardInterrupt caught, exiting.")
    finally:
        # 스크립트 종료 시 파이프라인을 정지시켜 자원을 정리합니다.
        # pipeline.set_state(Gst.State.NULL)
        for r in receivers:
            r.stop()
        cv2.destroyAllWindows()
        log("Pipeline stopped. Script finished.")

if __name__ == '__main__':
    main()