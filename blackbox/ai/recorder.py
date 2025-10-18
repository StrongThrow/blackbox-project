import os, time, threading, glob, datetime
from collections import deque
from pathlib import Path
import cv2, numpy as np

NUM_CAMS = 6
ENC_PARAMS = [int(cv2.IMWRITE_JPEG_QUALITY), 80]   # 메모리 절약

def _ts_str():
    return datetime.datetime.now().strftime("%Y%m%d_%H%M%S")

class TimeWindowEventRecorder6:
    """
    - 메인에서 6캠 동시 배치 1개 받을 때마다 push_batch(frames6, ts) 호출
    - trigger(tag) 호출 시: t0=현재시간 기준 [t0- pre_secs, t0 + post_secs] 윈도우 저장
    - FPS 의존 없음 (전적으로 ts 기반)
    """
    def __init__(self,
                 out_dir="event6",
                 size=(800, 450),
                 pre_secs=5.0,
                 post_secs=5.0,
                 retention_secs=60.0,    # 버퍼에 보관할 최대 시간 (메모리 제한용)
                 save_as="jpg",          # "jpg" | "mp4"
                 fourcc_str="mp4v"):
        self.out_dir = Path(out_dir)
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.size = tuple(size)
        self.pre_secs = float(pre_secs)
        self.post_secs = float(post_secs)
        self.retention_secs = float(retention_secs)
        self.save_as = save_as
        self.fourcc = cv2.VideoWriter_fourcc(*fourcc_str)

        # 시간 기반 링버퍼: [(ts, [jpg_cam0..cam5])]
        self.buffer = deque()
        self._lock = threading.Lock()

        # 진행 중 이벤트(단일 세션)
        self.active = False
        self.t0 = None
        self.event_id = None
        self._post_done = False

        # 비동기 저장 워커 (블로킹 방지)
        import queue
        self._jobs: "queue.Queue[tuple[list, str]]" = queue.Queue(maxsize=8)
        self._stop = False
        self._worker = threading.Thread(target=self._worker_loop, daemon=True)
        self._worker.start()

    # ------------- 외부에서 호출하는 API -------------
    def push_batch(self, frames6, ts=None):
        """
        frames6: 길이 6의 BGR np.ndarray 리스트
        ts: float epoch seconds (None이면 time.time())
        """
        if ts is None:
            ts = time.time()
        assert len(frames6) == NUM_CAMS, "frames6 must be length-6"

        # 리사이즈 & JPEG 인코딩
        jpg6 = []
        for fr in frames6:
            if fr.shape[1] != self.size[0] or fr.shape[0] != self.size[1]:
                fr = cv2.resize(fr, self.size)
            ok, jb = cv2.imencode(".jpg", fr, ENC_PARAMS)
            if not ok:
                continue
            jpg6.append(jb.tobytes())
        if len(jpg6) != NUM_CAMS:
            return

        with self._lock:
            self.buffer.append((ts, jpg6))
            self._prune_old_locked(ts)

            # 이벤트 진행 중이면 post 완료 체크
            if self.active and not self._post_done:
                if ts >= self.t0 + self.post_secs:
                    self._post_done = True
                    self._finalize_and_enqueue_locked()

    def trigger(self, tag: str = ""):
        """이벤트 시작(현재 시간을 기준으로 pre/post 윈도우 결정)"""
        with self._lock:
            if self.active:
                return  # 단일 세션 정책: 이미 진행 중이면 무시(동시 다중 필요 시 확장)
            self.active = True
            self.t0 = time.time()
            self._post_done = False
            self.event_id = f"{_ts_str()}{('_'+tag) if tag else ''}"
            # pre는 finalize 시점에 buffer에서 시간 조건으로 추출

    def close(self, wait: float = 5.0):
        """종료 시 호출(워커 정리)"""
        self._stop = True
        t0 = time.time()
        while not self._jobs.empty() and (time.time() - t0 < wait):
            time.sleep(0.05)
        self._worker.join(timeout=max(0.0, wait - (time.time() - t0)))

    # ------------- 내부 유틸 -------------
    def _prune_old_locked(self, now_ts):
        """retention_secs 보다 오래된 배치 제거(메모리 제한)"""
        cutoff = now_ts - self.retention_secs
        while self.buffer and self.buffer[0][0] < cutoff:
            self.buffer.popleft()

    def _finalize_and_enqueue_locked(self):
        """pre/post 구간을 시간조건으로 뽑아 저장작업 큐에 enqueue"""
        t0 = self.t0
        pre_from = t0 - self.pre_secs
        post_to = t0 + self.post_secs

        # 시간 조건으로 버퍼에서 추출
        window = [b for b in self.buffer if pre_from <= b[0] <= post_to]
        # 안전하게 시간 정렬
        window.sort(key=lambda x: x[0])

        # 아예 없으면 드롭
        if not window:
            # 상태 리셋
            self.active = False
            self.t0 = None
            self.event_id = None
            self._post_done = False
            return

        # 저장 경로
        event_dir = str(self.out_dir / self.event_id)

        # enqueue (워커가 실제 파일 저장)
        try:
            self._jobs.put_nowait((window, event_dir))
        except Exception:
            pass

        # 상태 리셋
        self.active = False
        self.t0 = None
        self.event_id = None
        self._post_done = False

    def _worker_loop(self):
        import queue
        while not self._stop or not self._jobs.empty():
            try:
                window, event_dir = self._jobs.get(timeout=0.1)
            except queue.Empty:
                continue
            try:
                self._save_window(window, event_dir)
            except Exception as e:
                print("[SAVE][ERR]", e)
            finally:
                self._jobs.task_done()

    def _save_window(self, window, event_dir):
        os.makedirs(event_dir, exist_ok=True)

        # 카메라별로 펼치기
        cam_streams = {i: [] for i in range(NUM_CAMS)}  # list[(ts, jpg_bytes)]
        for ts, jpg6 in window:
            for i in range(NUM_CAMS):
                cam_streams[i].append((ts, jpg6[i]))

        if self.save_as == "jpg":
            for cam in range(NUM_CAMS):
                cdir = Path(event_dir) / f"cam{cam}"
                cdir.mkdir(parents=True, exist_ok=True)
                for idx, (ts, jb) in enumerate(cam_streams[cam]):
                    with open(cdir / f"{idx:04d}_{int(ts*1000)}.jpg", "wb") as f:
                        f.write(jb)
        else:  # "mp4"
            for cam in range(NUM_CAMS):
                items = cam_streams[cam]
                if not items:
                    continue
                first = items[0][1]
                img = cv2.imdecode(np.frombuffer(first, dtype=np.uint8), cv2.IMREAD_COLOR)
                h, w = img.shape[:2]
                vw = cv2.VideoWriter(str(Path(event_dir)/f"cam{cam}.mp4"),
                                     self.fourcc,  # fps 모름 → 타임스탬프 간격으로 보간하려면 추가 로직 필요
                                     10,           # 임시 fps(재생 편의용). 진짜 시간축은 ts로 복원 가능.
                                     (w, h))
                if not vw.isOpened():
                    raise RuntimeError("VideoWriter open failed")
                for _, jb in items:
                    fr = cv2.imdecode(np.frombuffer(jb, dtype=np.uint8), cv2.IMREAD_COLOR)
                    vw.write(fr)
                vw.release()
        print(f"[SAVE] {event_dir}  (frames={len(window)})")
