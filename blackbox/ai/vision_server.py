
import os
import sys
import time
import threading
import json
import math
from queue import Queue

import numpy as np
import cv2
import torch
import onnxruntime as ort

#-------------------------------imsi-----------------------------------

import fps_calc
import multiprocessing
import pre_post_process
import core
import demo_manager
import async_api
import visualization
import recorder


# ---- Hailo ----
from hailo_platform import (HEF, Device, VDevice, HailoSchedulingAlgorithm)

# ---- GStreamer ----
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst


# =================== 설정 ===================
# 입력(카메라)
SRC_W, SRC_H = 800, 320
NUM_CAMS = 6
PORT0 = 5000  # udpsrc 시작 포트 (5000~5005)

# 모델 경로
MODELS_DIR = os.path.dirname(__file__)
BACKBONE_HEF    = os.path.join(MODELS_DIR, "petrv2_repvggB0_backbone_pp_800x320.hef")
TRANSFORMER_HEF = os.path.join(MODELS_DIR, "petrv2_repvggB0_transformer_pp_800x320.hef")
POSTPROC_ONNX   = os.path.join(MODELS_DIR, "petrv2_postprocess.onnx")
MATMUL_NPY      = os.path.join(MODELS_DIR, "matmul.npy")
MAP_PATH       = os.path.join(MODELS_DIR, "map.png")
MAX_QUEUE_SIZE = 3
# BEV 렌더링
BEV_SIZE = 640
BUF_BEV_MAP = BEV_SIZE * 1.5 # 회전 생각해서
XY_RANGE_M = 61.2
LINE_W = 2  # pixel
CARLA_POS_MAX = 230 #pixel
CARLA_POS_MIN = -230 #pixel
MAP_SIZE = 8192  # pixels
MAP_SCALE = MAP_SIZE / (CARLA_POS_MAX - CARLA_POS_MIN)  

# 이벤트 비트 설정
EVENT_ACCEL         = 1 << 0
EVENT_BRAKE         = 1 << 1
EVENT_PEDESTRIAN    = 1 << 2
EVENT_TRUCK         = 1 << 3
EVENT_MOTORCYCLE    = 1 << 4
EVENT_PUNK          = 1 << 5 
EVENT_TEMP1         = 1 << 6
EVENT_TEMP2         = 1 << 7

def log(msg: str):
    print(f"[Py LOG] {msg}", file=sys.stderr, flush=True)


# =================== 유틸 (qparam/양자화/BEV) ===================
import queue as _queue

def _np_to_py(obj):
    import numpy as _np
    if isinstance(obj, _np.ndarray):
        return obj.tolist()
    if isinstance(obj, _np.generic):
        return obj.item()
    if isinstance(obj, dict):
        return {k: _np_to_py(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_np_to_py(v) for v in obj]
    return obj
def q1(x): return float(np.around(x, 1))  
def _build_json_msg(dets_dict, meta=None):
 
    objs = []
    try:
        if isinstance(dets_dict, dict) and dets_dict.get('pts_bbox'):
            item0 = dets_dict['pts_bbox'][0] or {}
            boxes  = np.asarray(item0.get("boxes_3d", []))
            scores = np.asarray(item0.get("scores_3d", []))
            labels = np.asarray(item0.get("labels_3d", []))

            if boxes.ndim == 1:  # (7,) 같은 단일 케이스 방어
                boxes = boxes.reshape(1, -1)

            N = boxes.shape[0] if boxes.size else 0
            for i in range(N):
                b = boxes[i]
                l = labels[i]
                obj = {
                    "label": _np_to_py(l),      
                    "x":     q1(b[0]),  # 0번
                    "y":     q1(b[1]),  # 1번
                    "ax": q1(b[7]),  # 7번
                    "ay":  q1(b[8]),  # 8번 
                }
                log(f"[DEBUG] box {i}: {obj}")  # 디버그 로그
                if i < len(scores): obj["score"] = _np_to_py(scores[i])
                if i < len(labels): obj["label"] = _np_to_py(labels[i])
                objs.append(obj)
    except Exception as e:
        # 디코드가 비정상이면 빈 배열 + 상태만 리턴
        pass

    return objs          # ← 요청한 배열 키
    


def json_sender_proc(det_q):
    import sys, json
    import pre_post_process as pp
    while True:
        try:
            item = det_q.get(timeout=1)
        except _queue.Empty:
            continue

        meta = None
        if isinstance(item, tuple) and len(item) == 2:
            pp_output, meta = item
            try:
                dets_dict = pp.decode(pp_output)
            except Exception:
                dets_dict = {}
        else:
            dets_dict = item if isinstance(item, dict) else {}

        obj = {
            "objects": _build_json_msg(dets_dict, meta=meta),
        }
        log(f"[JSON Sender] Sending {len(obj['objects'])} objects") 
        log(f"[DEBUG] JSON: {type(obj)}")  
        # ✅ stdout에는 JSON 한 줄만! (부모가 파싱하기 쉽게)
        print(json.dumps(obj, ensure_ascii=False))
        sys.stdout.flush()

def parse_command(line: str):
    """
    'analyze {json}' 형태에서 payload만 파싱 (없어도 동작은 하게 관대하게 처리)
    """
    line = line.strip()
    if not line:
        return None, None
    
    if line.startswith("analyze"):

        payload = None
        # 'analyze ' 뒤에 JSON이 붙어 있을 수도 있음
        if len(line) > len("analyze"):
            rest = line[len("analyze"):].strip()
            if rest:
                try:
                    payload = json.loads(rest)
                except Exception:
                    payload = None
        return "analyze", payload
    

    elif line.startswith("draw"):

        payload = None
        # 'analyze ' 뒤에 JSON이 붙어 있을 수도 있음
        if len(line) > len("draw"):
            rest = line[len("draw"):].strip()
            if rest:
                try:
                    payload = json.loads(rest)
                except Exception:
                    payload = None
        return "draw", payload
    


# === Add to vision_server.py (상단 유틸 근처) ===
import math
import numpy as np
import cv2

def _rot_rect_corners(cx, cy, w, l, yaw):
    # 중심(cx,cy), 폭(w), 길이(l), 라디안 yaw(차량 진행방향이 +x 기준)
    # 4코너 (앞-왼, 앞-오, 뒤-오, 뒤-왼) 시계방향
    hw, hl = w/2.0, l/2.0
    corners = np.array([
        [ +hl, +hw],  # front-right (x forward, y left 기준이면 순서 맞춰도 무방)
        [ +hl, -hw],
        [ -hl, -hw],
        [ -hl, +hw],
    ], dtype=np.float32)
    c, s = math.cos(yaw), math.sin(yaw)
    R = np.array([[c,-s],[s,c]], dtype=np.float32)
    rot = (R @ corners.T).T
    rot[:,0] += cx
    rot[:,1] += cy
    return rot  # (4,2) in meters (BEV 좌표계)

def _meters_to_pixels(xy, origin_px, scale):
    # origin_px = (ox, oy), scale = px per meter
    xy_px = xy.copy()
    xy_px[:,0] = origin_px[0] + xy[:,0]*scale
    xy_px[:,1] = origin_px[1] - xy[:,1]*scale  # y축 뒤집기(위가 +)
    return xy_px.astype(np.int32)

def make_bev_canvas(size=640, xy_range=61.2):
    bev = np.ones((size, size, 3), np.uint8)*255
    # 중심 십자선/격자
    ox = oy = size//2
    cv2.line(bev, (0, oy), (size, oy), (220,220,220), 1)
    cv2.line(bev, (ox, 0), (ox, size), (220,220,220), 1)
    # 10m grid
    scale = size/(2*xy_range)
    for m in range(-60, 61, 10):
        y = int(oy - m*scale)
        x = int(ox + m*scale)
        cv2.line(bev, (0,y), (size,y), (240,240,240), 1)
        cv2.line(bev, (x,0), (x,size), (240,240,240), 1)
    # ego box
    cv2.rectangle(bev, (ox-5, oy-10), (ox+5, oy+10), (0,255,0), 2)
    return bev

def draw_bev_boxes_on(canvas, dets, score_thresh=0.30, xy_range=61.2):
    # dets: pre_post_process.decode() 결과(dict)
    if not dets or not dets.get('pts_bbox'): 
        return canvas
    item = dets['pts_bbox'][0]
    boxes3d = np.asarray(item.get('boxes_3d', []))  # (N, >=7) = [cx, cy, cz, w, l, h, yaw, ...]
    scores  = np.asarray(item.get('scores_3d', []))
    labels  = np.asarray(item.get('labels_3d', []))

    if boxes3d.size == 0:
        return canvas

    H, W = canvas.shape[:2]
    ox = oy = W//2
    scale = W/(2*xy_range)

    for i in range(len(boxes3d)):
        s = float(scores[i]) if i < len(scores) else 1.0
        if labels[i] == 4 or labels[i] == 5 or labels[i] == 9 :
            continue
        if s < score_thresh:
            continue
        cx, cy, cz, l, w, h, yaw = boxes3d[i, :7]

        # 범위 밖이면 스킵
        if abs(cx) > xy_range or abs(cy) > xy_range:
            continue

        # 코너 4점(m) → 픽셀
        corners_m = _rot_rect_corners(cx, cy, w, l, -yaw)
        corners_px = _meters_to_pixels(corners_m, (ox, oy), scale)

        # 박스
        cv2.polylines(canvas, [corners_px], isClosed=True, color=(0,0,255), thickness=2)

        # 진행방향(앞쪽 에지의 중점)
        front_mid_m = (corners_m[0] + corners_m[1]) / 2.0
        center_px = _meters_to_pixels(np.array([[cx,cy]]), (ox,oy), scale)[0]
        front_px  = _meters_to_pixels(np.array([front_mid_m]), (ox,oy), scale)[0]
        cv2.line(canvas, center_px, front_px, (0,0,255), 2)

        # 라벨/점수
        cv2.putText(canvas, f"{int(labels[i])}:{s:.2f}", front_px, cv2.FONT_HERSHEY_SIMPLEX, 0.4, (20,20,20), 1, cv2.LINE_AA)

    return canvas

# === Add to vision_server.py ===


# === 3x2 모자이크 유틸 ===
def draw_cam_index(img, idx):
    cv2.putText(img, f"Cam {idx}", (8, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0,0,0), 3, cv2.LINE_AA)
    cv2.putText(img, f"Cam {idx}", (8, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255,255,255), 1, cv2.LINE_AA)
    return img

def make_mosaic_grid(img_list, rows=2, cols=3, tile_wh=(400,160), pad=4, order=None, draw_index=True):
    """
    img_list: [H,W,3] BGR 이미지 리스트 (최대 rows*cols개)
    tile_wh: 한 타일 크기 (w,h)
    pad: 타일 간 여백(px)
    order: [이미지 인덱스 배치 순서], 예) [0,1,2,3,4,5]
    """
    if order is None:
        order = list(range(len(img_list)))
    # 캔버스 크기 계산
    tw, th = tile_wh
    grid_w = cols * tw + (cols + 1) * pad
    grid_h = rows * th + (rows + 1) * pad
    canvas = np.zeros((grid_h, grid_w, 3), np.uint8)

    # 배치
    for k in range(rows * cols):
        r = k // cols
        c = k % cols
        x0 = pad + c * (tw + pad)
        y0 = pad + r * (th + pad)

        if k < len(order) and order[k] < len(img_list) and img_list[order[k]] is not None:
            tile = cv2.resize(img_list[order[k]], (tw, th))
            if draw_index:
                tile = draw_cam_index(tile, order[k])
        else:
            tile = np.zeros((th, tw, 3), np.uint8)

        canvas[y0:y0+th, x0:x0+tw] = tile

    # 테두리
    cv2.rectangle(canvas, (0,0), (grid_w-1, grid_h-1), (40,40,40), 1)
    return canvas

def bev_viz_proc(in_queue,payload,win='BEV'):
    import cv2, numpy as np
    import pre_post_process as pp
    last_dets = None  # 마지막 디텍션 유지

    try:
        item = in_queue.get(timeout=0.5)
        if isinstance(item, tuple) and len(item) == 2:
            pp_output, meta = item
            last_dets = pp.decode(pp_output)
        else:
            last_dets = item
    except _queue.Empty:
        # 새 입력이 없어도 이전/빈 캔버스를 계속 렌더
        pass
    # value
    # speed
    # tires
    # 로 올거임
    payload['speed']
    bev = make_bev_canvas(size=640, xy_range=XY_RANGE_M)
    if last_dets:
        bev = draw_bev_boxes_on(bev, last_dets, score_thresh=0.3, xy_range=XY_RANGE_M)

    cv2.imshow(win, bev)


# =================== JSON




# =================== GStreamer 수신 ===================
class GstVideoReceiver:
    def __init__(self, port: int):
        self.port = port
        self.pipeline = None
        self.appsink = None
        self.latest_frame = None
        self._stop = False
        self._th = None

    def init_pipeline(self):
        # appsink를 BGR로 맞춤
        pipeline_str = (
            f"udpsrc port={self.port} ! "
            "application/x-rtp,media=video,encoding-name=H264,payload=96 ! "
            "rtpjitterbuffer latency=60 ! "
            "rtph264depay ! h264parse config-interval=-1 ! "
            "avdec_h264 max-threads=0 ! videoconvert ! "
            "queue max-size-buffers=5 ! "  # 추가
            "video/x-raw,format=BGR ! appsink name=sink drop=true max-buffers=1 sync=false"
        )
        self.pipeline = Gst.parse_launch(pipeline_str)
        self.appsink = self.pipeline.get_by_name("sink")
        self.pipeline.set_state(Gst.State.PLAYING)

    def _pull(self):
        # 일부 환경은 try_pull_sample 바인딩이 없어서 action-signal 사용
        sample = self.appsink.emit("try-pull-sample", 50 * Gst.MSECOND)
        if not sample:
            return None
        buf = sample.get_buffer()
        caps = sample.get_caps()
        s = caps.get_structure(0)
        w = int(s.get_value("width"))
        h = int(s.get_value("height"))
        ok, mapinfo = buf.map(Gst.MapFlags.READ)
        if not ok:
            return None
        try:
            arr = np.frombuffer(mapinfo.data, dtype=np.uint8)
            return arr.reshape((h, w, 3))
        finally:
            buf.unmap(mapinfo)

    def _loop(self):
        import time
        idle_sleep = 0.005   # 5ms만 쉬어도 효과 큼
        while not self._stop:
            f = self._pull()
            if f is None:
                time.sleep(idle_sleep)   # ← 이 한 줄이 코어 100%를 크게 내림
                continue
            self.latest_frame = f

    def start(self):
        self._stop = False
        self._th = threading.Thread(target=self._loop, daemon=True)
        self._th.start()

    def stop(self):
        self._stop = True
        if self._th:
            self._th.join()
        if self.pipeline:
            self.pipeline.set_state(Gst.State.NULL)

det_for_json_q = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)
det_for_bev_q  = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)

STOP = ("__STOP__", None)

def fanout_proc(src_q, dst_qs):
    import queue as _q
    while True:
        try:
            item = src_q.get(timeout=0.5)
        except _q.Empty:
            continue
        # if item == STOP:
        #     # 자식들에도 STOP 전달
        #     for q in dst_qs:
        #         try: q.put_nowait(STOP)
        #         except: pass
        #     break
        # 각 목적지 큐로 non-blocking 전송 (풀이면 드롭)
        for q in dst_qs:
            try: q.put_nowait(item)
            except: pass
# =================== 메인 ===================
def main():
    log("Simple BEV server starting")

    # GStreamer 시작
    Gst.init(None)
    receivers = [GstVideoReceiver(PORT0 + i) for i in range(NUM_CAMS)]
    for r in receivers:
        r.init_pipeline()
        r.start()
    
    # Map 데이터 로드
    
    map_image = cv2.imread(MAP_PATH)
    map_child = None

    # Recorder 초기화 시작
    # rec = recorder.ENC_PARAMSTimeWindowEventRecorder6(
    #     out_dir="/home/root/blackbox/event6",
    #     size=(800, 450),
    #     pre_secs=5.0, post_secs=5.0,
    #     retention_secs=60.0,
    #     save_as="jpg"  # jpg가 디버깅/속도에 유리. mp4 원하면 "mp4"
    # )
    # Recorder 초기화 끝
    

    fps_calculator = fps_calc.FPSCalc(2)
    queues = []
    bb_tranformer_meta_queue = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)
    transformer_pp_meta_queue = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)
    bb_tranformer_queue = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)
    transformer_pp_queue = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)
    pp_3dnms_queue = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)
    nms_send_queue = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)


    queues.append(bb_tranformer_meta_queue)
    queues.append(transformer_pp_meta_queue)
    queues.append(bb_tranformer_queue)
    queues.append(transformer_pp_queue)
    queues.append(pp_3dnms_queue)
    queues.append(nms_send_queue)
    # Hailo VDevice
    
    manager = multiprocessing.Manager()
    demo_mng = demo_manager.DemoManager(manager)
    devices = Device.scan()
    params = async_api.create_vdevice_params()
    threads = []
    processes = []
    params = VDevice.create_params()
    params.scheduling_algorithm = HailoSchedulingAlgorithm.ROUND_ROBIN 

    with VDevice(params) as target:
        
        # --- Backbone (in UINT8 / out UINT8)
       
        camera_in_q = multiprocessing.Queue(maxsize=MAX_QUEUE_SIZE)  # 카메라 프레임을 백본으로 넘기는 큐

        threads.append(threading.Thread(target=core.backbone_from_cam, args=(target, camera_in_q, BACKBONE_HEF, bb_tranformer_queue,
                                    bb_tranformer_meta_queue, demo_mng, True)))
        
        threads.append(threading.Thread(target=core.transformer, args=(target, TRANSFORMER_HEF, MATMUL_NPY, bb_tranformer_queue, bb_tranformer_meta_queue, transformer_pp_queue, transformer_pp_meta_queue, demo_mng)))

        processes.append(multiprocessing.Process(target=pre_post_process.post_proc,
                                                    args=(transformer_pp_queue, transformer_pp_meta_queue,
                                                    pp_3dnms_queue, POSTPROC_ONNX, demo_mng)))

        # processes.append(multiprocessing.Process(target=pre_post_process.d3nms_proc,
        #                                         args=(pp_3dnms_queue, nms_send_queue, nusc, demo_mng)))
        # processes.append(multiprocessing.Process(target=visualization.viz_proc,
        #                                 args=(args.input, args.data, nms_send_queue,
        #                                 fps_calculator, nusc)))
        # 스타트는 한 번만
        log("multi process starting...")
        for t in threads: t.start()
        for p in processes: p.start()

        # fanout
        fan = multiprocessing.Process(target=fanout_proc, args=(pp_3dnms_queue, [det_for_json_q, det_for_bev_q]))
        fan.daemon = False
        fan.start()

        # 각 소비자에는 자기 큐만 전달
        json_out = multiprocessing.Process(target=json_sender_proc, args=(det_for_json_q,))
        json_out.daemon = False
        json_out.start()

        # bev_proc = multiprocessing.Process(target=bev_viz_proc, args=(det_for_bev_q,), daemon=False)
        # bev_proc.start()
        token  = 0
        recodCMD = 0

        # BEV 그려주는걸 따로 빼야하니깐 처음 만들어줌
        win = 'BEV'
        log("[BEV] proc start")  # 디버그 로그
        cv2.namedWindow(win, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(win, 700, 700)
        log("init done")
        try :
            while True:

                line = sys.stdin.readline()# 0.2 초를 보장해줌
                
                if not line:
                    log("stdin closed. exiting.")
                    break

                cmd, payload = parse_command(line)
                if cmd != "analyze":
                    log(f"ignored line: {line.strip()}")
                    continue

                if cmd == "analyze":
                    recodCMD = recodCMD + 1
                    # 1) 6캠 프레임 수집 + 예제 동일 전처리(800x450 → y=130~450 크롭 → 800x320)
                    images_after_pre = []
                    images_record = []
                    for i in range(NUM_CAMS):
                        f = receivers[i].latest_frame
                        if f is None:
                            f = np.zeros((SRC_H, SRC_W, 3), np.uint8)
                        img = cv2.resize(f, (800, 450))
                        images_record.append(img.copy())
                        x, y, width, height = 0, 130, 800, 450  
                        img = img[y:height, x:x + width]

                        images_after_pre.append(img)

                    #rec.push_batch(images_after_pre)
                    cam_order = [0, 1, 2, 3, 4, 5]

                    # 원본(800x320)을 바로 키우면 화면이 너무 커지니 적당히 축소
                    # tile_wh=(400,160) → 전체 약 (3*400 + 여백) x (2*160 + 여백) ≈ 1220x344
                    mosaic = make_mosaic_grid(
                        images_after_pre,
                        rows=2, cols=3,
                        tile_wh=(400, 160),
                        pad=6,
                        order=cam_order,
                        draw_index=True
                    )
                    cv2.imshow("Cams 3x2", mosaic)
                    cv2.waitKey(1)
                    log(f"[Main] Got {len(images_after_pre)} frames")

                    frames_np = np.asarray(images_after_pre, dtype=np.uint8)  
                    #token = time.time_ns()
                    token = token + 1
                    
                    try:
                        camera_in_q.put((frames_np, {"token": token}), block=False)
                        now = time.time()
                        localtime = time.localtime(now)
                        hi = time.strftime("%Y-%m-%d %H:%M:%S",localtime)
                        log(f"{hi}[Main] size : {camera_in_q.qsize()}")
                    except:
                        _ = camera_in_q.get()  # 가장 오래된 프레임 드롭
                        log("[Main] WARN: camera_in_q full, dropping frame")
                        pass
                        
                    #print(json.dumps({"status": "success", "detections": detections}, ensure_ascii=False))
                    # sys.stdout.flush()
                    log("wating draw cmd...")
                    line = sys.stdin.readline()# draw   
                    cmd, payload = parse_command(line)
                    if cmd != "draw":
                        log(f"ignored line: {line.strip()}")
                        continue
                    log(f"received cmd: {cmd}")
                    if cmd == "draw":
                        log(f"[BEV] draw cmd received")  # 디버그 로그
                        log(f"[BEV] payload: {payload}")  # 디버그 로그
                        bev_viz_proc(det_for_bev_q, payload, win)# 그려주는건 따로 
                        log("end draw ...")

            
                print("done", flush=True)        

        except KeyboardInterrupt:
            demo_mng.set_terminate()

        finally:
            # STOP 브로드캐스트
            for q in (pp_3dnms_queue, det_for_json_q, det_for_bev_q):
                try: q.put_nowait(STOP)
                except: pass

            # 스레드/프로세스 조인
            for t in threads: t.join(timeout=1)
            for p in processes: p.join(timeout=2)
            #fan.join(timeout=1); json_out.join(timeout=1); bev_proc.join(timeout=1)

            # 수신기 정지
            for r in receivers: r.stop()

            cv2.destroyAllWindows()

    log("Done.")


if __name__ == "__main__":
    main()
