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
from gi.repository import Gst, GstApp, GstVideo
# LDH gi클래스 구현을 위한 import 끝



# ===== 입력(카메라) =====
SRC_W, SRC_H = 800, 480
ROWS, COLS   = 2, 3
NUM_CAMERAS  = ROWS * COLS  # 6

# ===== 출력(타일) =====
TILE_W, TILE_H = 266, 240
OUT_W, OUT_H   = 800 , 480
OUT_FPS        = 15


SINK_PIPELINE = "videoconvert ! kmssink sync=false"
SINK_CAPS = f"video/x-raw,format=RGBx,width={OUT_W},height={OUT_H},framerate={OUT_FPS}/1"


# GStreamer 및 GObject 라이브러리 로드.
gi.require_version('Gst', '1.0')
gi.require_version('GstApp', '1.0')     # 추가 
gi.require_version('GstVideo', '1.0')   # 추가

# LDH GstVideoReceiver class 선언 시작
class GstVideoReceiver:
    def __init__(self, port):
        self.port           = port
        self.pipeline       = None
        self.appsink        = None
        self.bus            = None
        self.is_initialized = False
        self.latest_frame   = None
        self._stop          = False
        self._thread        = None

    def init_pipeline(self):
        pipeline_str = (
            f"udpsrc port={self.port} ! "
            "application/x-rtp,media=video,encoding-name=H264,payload=96 ! "
            "rtpjitterbuffer latency=60 ! "
            "rtph264depay ! h264parse config-interval=-1 ! "
            "avdec_h264 max-threads=1 ! "
            "videoconvert ! "
            "video/x-raw,format=RGB ! "   # appsink에 RGB로 전달
            "appsink name=sink drop=true max-buffers=1 sync=false"
        )
        self.pipeline = Gst.parse_launch(pipeline_str)
        self.bus      = self.pipeline.get_bus()
        self.appsink  = self.pipeline.get_by_name("sink")

        ret = self.pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            self.pipeline.set_state(Gst.State.NULL)
            raise RuntimeError(f"RX pipeline start failed on port {self.port}")
        self.is_initialized = True
        self._drain_bus()
        return True

    def _drain_bus(self):
        while True:
            msg = self.bus.pop()
            if not msg:
                break
            if msg.type == Gst.MessageType.ERROR:
                err, dbg = msg.parse_error()
                print(f"[RX:{self.port}][ERROR] {err.message}  debug={dbg}")
            elif msg.type == Gst.MessageType.WARNING:
                w, dbg = msg.parse_warning()
                print(f"[RX:{self.port}][WARN] {w.message}  debug={dbg}")

    def _pull_frame(self):
        sample = self.appsink.try_pull_sample(50 * Gst.MSECOND)
        if not sample:
            self._drain_bus()
            return None

        buf  = sample.get_buffer()
        caps = sample.get_caps()
        s    = caps.get_structure(0)
        w, h = int(s.get_value("width")), int(s.get_value("height"))
        fmt  = s.get_value("format")
        #print(f"[RX:{self.port}] got frame {w}x{h} fmt={fmt}")  # 디버그 로그

        ok, mapinfo = buf.map(Gst.MapFlags.READ)
        if not ok:
            return None

        try:
            data = np.frombuffer(mapinfo.data, dtype=np.uint8)
            frame = data.reshape((h, w, 3))   # RGB 3채널
            return frame
        finally:
            buf.unmap(mapinfo)

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
            self._thread = None

        if self.is_initialized and self.pipeline:
            # 상태 NULL로 전환
            self.pipeline.set_state(Gst.State.NULL)
            # 전환 완료 대기 (중요)
            self.pipeline.get_state(Gst.CLOCK_TIME_NONE)
            # 레퍼런스 정리
            self.appsink = None
            self.bus = None
            self.pipeline = None
            self.is_initialized = False

# LDH GstVideoReceiver class 선언 끝
# LDH DisplayRGB class 선언 시작

class DisplayRGB:
    def __init__(self):
        desc = (
            "appsrc name=src is-live=true do-timestamp=true format=time "
            f"caps={SINK_CAPS} ! queue ! {SINK_PIPELINE}"
        )
        self.pipeline = Gst.parse_launch(desc)
        self.bus      = self.pipeline.get_bus()
        self.appsrc   = self.pipeline.get_by_name("src")

        ret = self.pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            self.pipeline.set_state(Gst.State.NULL)
            raise RuntimeError("Display pipeline start failed (RGB)")
        self._drain_bus()

    def _drain_bus(self):
        while True:
            msg = self.bus.pop()
            if not msg:
                break
            if msg.type == Gst.MessageType.ERROR:
                err, dbg = msg.parse_error()
                print(f"[Display][ERROR] {err.message}  debug={dbg}")
            elif msg.type == Gst.MessageType.WARNING:
                w, dbg = msg.parse_warning()
                print(f"[Display][WARN] {w.message}  debug={dbg}")

    def push_rgb(self, frame_bgr):
        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        frame_rgbx = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2RGBA)   # RGBA = RGBx
        frame_rgbx = np.ascontiguousarray(frame_rgbx, dtype=np.uint8)

        buf = Gst.Buffer.new_wrapped(frame_rgbx.tobytes())
        ts = int(time.time() * Gst.SECOND)
        buf.pts = ts
        buf.dts = ts
        buf.duration = Gst.SECOND // OUT_FPS

        self.appsrc.emit("push-buffer", buf)

    def close(self):
        if not self.pipeline:
            return
        try:
            # 먼저 EOS로 파이프라인에 종료를 알림 (권장)
            try:
                self.appsrc.end_of_stream()
            except Exception:
                pass
            # NULL 전이
            self.pipeline.set_state(Gst.State.NULL)
            self.pipeline.get_state(Gst.CLOCK_TIME_NONE)
        finally:
            self.appsrc = None
            self.bus = None
            self.pipeline = None
# LDH DisplayRGB class 선언 끝

# LDH imsi로 띄워줄거 시간이이된다면 Map할때 수정될 것 
def tile_grid_rgb(frames, rows=ROWS, cols=COLS, tile_w=TILE_W, tile_h=TILE_H):
    total = rows * cols
    padded = list(frames)[:total]
    while len(padded) < total:
        padded.append(np.zeros((SRC_H, SRC_W, 3), dtype=np.uint8))  # RGB 검정
    tiles = []
    idx = 0
    for _ in range(rows):
        row_imgs = []
        for _ in range(cols):
            f = padded[idx]
            if f is None:
                f = np.zeros((SRC_H, SRC_W, 3), dtype=np.uint8)
            row_imgs.append(cv2.resize(f, (tile_w, tile_h)))
            idx += 1
        tiles.append(np.hstack(row_imgs))
    return np.vstack(tiles)  # RGB

# ---------------------------LDH model input 을 위한 복사 및 변환 시작
def resize_with_letterbox(img, target_w=800, target_h=320):
    h, w = img.shape[:2]
    scale = min(target_w / w, target_h / h)
    new_w, new_h = int(w * scale), int(h * scale)

    resized = cv2.resize(img, (new_w, new_h))
    canvas = np.zeros((target_h, target_w, 3), dtype=np.uint8)
    top = (target_h - new_h) // 2
    left = (target_w - new_w) // 2
    canvas[top:top+new_h, left:left+new_w] = resized
    return canvas

def normalize_imagenet(img):
    img = img.astype(np.float32) / 255.0
    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    std  = np.array([0.229, 0.224, 0.225], dtype=np.float32)
    img = (img - mean) / std
    return img

def copy_for_model(displayFrames, target_w=800, target_h=320):
    processed = []
    for img in displayFrames:
        if img is None:
            img = np.zeros((target_h, target_w, 3), dtype=np.uint8)  # 빈 화면 대체
        img_proc = resize_with_letterbox(img, target_w, target_h)
        img_proc = normalize_imagenet(img_proc)
        img_proc = np.transpose(img_proc, (2, 0, 1))
        processed.append(img_proc)

    modelFrames = np.stack(processed, axis=0)
    modelFrames = np.expand_dims(modelFrames, axis=0)
    return modelFrames
# ---------------------------LDH model input 을 위한 복사 및 변환 끝


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
    receivers = [GstVideoReceiver(5000 + i) for i in range(NUM_CAMERAS)]
    for r in receivers:
        r.init_pipeline()
        r.start()

    display = DisplayRGB()
    # LDH pipeline을 통해 이미지 받는 객체 생성 끝

    log("GStreamer pipeline is running. Waiting for commands from C via stdin.")

    try:
        period = 1.0 / OUT_FPS
        next_t = time.time()
        # --- 2. C로부터의 명령을 기다리는 메인 루프 ---
        while True:
            # sys.stdin.readline()은 C에서 명령을 보낼 때까지 여기서 실행을 멈추고 대기합니다.
            # 이것이 이 스크립트의 기본 '대기 상태'입니다.
            #command = sys.stdin.readline()
            command = "analyze"
            if not command:
                log("C process closed the pipe. Exiting.")
                break
            
            log(f"Received command from C: {command.strip()}")
            
            if "analyze" in command.strip():
                #frame = get_single_frame(appsink)
                displayFrames = [] #이미지 넣을 리스트
                modelFrames = []
                displayFrames = [r.latest_frame for r in receivers]   # 각 프레임: RGB 또는 None
                
                for i in range(len(displayFrames)):
                    if displayFrames[i] is None:
                        displayFrames[i] = np.zeros((SRC_H, SRC_W, 3), dtype=np.uint8)
                    else:
                        displayFrames[i] = cv2.cvtColor(displayFrames[i], cv2.COLOR_BGR2RGB)
                displayFrames = np.array(displayFrames)  # numpy 형태여야 shape도 가능함
                #frames = np.transpose(frames, (0, 3, 1, 2)) 
                modelFrames = copy_for_model(displayFrames)# 1,6,3,320,800로 변환 될것임

                grid   = tile_grid_rgb(displayFrames, ROWS, COLS, TILE_W, TILE_H)  # RGB

                grid = cv2.resize(grid, (OUT_W, OUT_H), interpolation=cv2.INTER_LINEAR)

                display.push_rgb(grid)  # RGB 그대로 출력 imsi UI나중에 만들어지면 그때 다시

                if modelFrames is not None:
                    # [ 여기에 실제 AI 모델 추론 코드를 삽입합니다. ]
                    ai_result = {"status": "success", "frame_shape": modelFrames.shape}
                    log(f"Frame analysis complete. Shape: {modelFrames.shape}")
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
        
        if display:
            display.close()    
        cv2.destroyAllWindows()
        log("Pipeline stopped. Script finished.")

if __name__ == '__main__':
    main()