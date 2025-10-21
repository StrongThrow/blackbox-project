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
                 fourcc_str="mp4v",                 
                 target_fps=5.0,           # ← 추가: 저장/표시 FPS 고정
                 exact_count=True
                 ):
        self.out_dir = Path(out_dir)
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.size = tuple(size)
        self.pre_secs = float(pre_secs)
        self.post_secs = float(post_secs)
        self.retention_secs = float(retention_secs)
        self.save_as = save_as
        self.fourcc = cv2.VideoWriter_fourcc(*fourcc_str)


        self.target_fps = float(target_fps)
        self.exact_count = bool(exact_count)

        # 시간 기반 링버퍼: [(ts, [jpg_cam0..cam5])]
        self.buffer = deque()
        self._lock = threading.Lock()

        # 진행 중 이벤트(단일 세션)
        self.active = False
        self.t0 = None
        self.event_id = None
        self._post_done = False

        # exact_count 모드에서 사용: 필요/수집 개수
        self._pre_needed = 0
        self._post_needed = 0
        self._post_got = 0

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

            if self.active and self.exact_count:
                # t0 이후 프레임 개수 카운팅
                if ts > self.t0:
                    self._post_got += 1
                # 이후 프레임이 목표 개수에 도달하면 finalize
                if self._post_got >= self._post_needed:
                    self._finalize_and_enqueue_locked()
            elif self.active and not self.exact_count:
                # (구) 시간 기반 모드가 필요하면 여기서 처리할 수 있음
                if ts >= self.t0 + self.post_secs:
                    self._finalize_and_enqueue_locked()

    def trigger(self, tag: str = ""):
        """이벤트 시작(현재 시간을 기준으로 pre/post 윈도우 결정)"""
        with self._lock:
            if self.active:
                return  # 단일 세션 정책: 이미 진행 중이면 무시(동시 다중 필요 시 확장)
            self.active = True
            self.t0 = time.time()
            self.event_id = f"{_ts_str()}{('_'+tag) if tag else ''}"

            # exact_count 모드: 프레임 개수로 선/후롤 목표 설정
            if self.exact_count:
                self._pre_needed  = int(round(self.pre_secs  * self.target_fps))
                self._post_needed = int(round(self.post_secs * self.target_fps))
                self._post_got = 0   # t0 이후로 들어오는 배치 개수

    def close(self, wait: float = 5.0):
        self._stop = True
        t0 = time.time()
        while not self._jobs.empty() and (time.time() - t0 < wait):
            time.sleep(0.05)
        self._worker.join(timeout=max(0.0, wait - (time.time() - t0)))

    # ------------- 내부 유틸 -------------
    def _prune_old_locked(self, now_ts):
            cutoff = now_ts - self.retention_secs
            while self.buffer and self.buffer[0][0] < cutoff:
                self.buffer.popleft()

    def _take_last_with_pad(self, seq, n, pad_with=None):
        """
        seq: list[(ts, jpg6)]
        n개가 모자라면 pad_with(없으면 seq의 첫 원소)를 앞쪽으로 복제
        """
        if n <= 0:
            return []
        if len(seq) >= n:
            return seq[-n:]
        if not seq:
            if pad_with is None:
                return []
            return [pad_with] * n
        need = n - len(seq)
        pad = [ (seq[0] if pad_with is None else pad_with) ] * need
        return pad + seq

    def _take_first_with_pad(self, seq, n, pad_with=None):
        """
        seq: list[(ts, jpg6)]
        n개가 모자라면 pad_with(없으면 seq의 마지막 원소)를 뒤쪽으로 복제
        """
        if n <= 0:
            return []
        if len(seq) >= n:
            return seq[:n]
        if not seq:
            if pad_with is None:
                return []
            return [pad_with] * n
        need = n - len(seq)
        pad = [ (seq[-1] if pad_with is None else pad_with) ] * need
        return seq + pad
    
    def _finalize_and_enqueue_locked(self):
        t0 = self.t0

        if self.exact_count:
            # 버퍼를 리스트로 변환 후, t0 기준으로 분할
            buf = list(self.buffer)
            pre  = [b for b in buf if b[0] <= t0]
            post = [b for b in buf if b[0] >  t0]

            # pre는 '마지막 pre_needed개', post는 '처음 post_needed개'를 확보(부족하면 패딩)
            # 패딩 기준 프레임은 서로 맞물리게: pre쪽은 post의 첫 프레임, post쪽은 pre의 마지막 프레임을 사용
            pad_for_pre  = post[0] if post else (pre[-1] if pre else None)
            pad_for_post = pre[-1] if pre else (post[0] if post else None)

            pre_sel  = self._take_last_with_pad(pre,  self._pre_needed,  pad_with=pad_for_pre)
            post_sel = self._take_first_with_pad(post, self._post_needed, pad_with=pad_for_post)

            window = pre_sel + post_sel

        else:
            # (구) 시간 기반 윈도우
            pre_from = t0 - self.pre_secs
            post_to  = t0 + self.post_secs
            window = [b for b in self.buffer if pre_from <= b[0] <= post_to]
            window.sort(key=lambda x: x[0])

        # 아예 없으면 리셋
        if not window:
            self._reset_event_locked()
            return

        event_dir = str(self.out_dir / self.event_id)
        try:
            self._jobs.put_nowait((window, event_dir))
        except Exception:
            pass

        self._reset_event_locked()

    def _reset_event_locked(self):
        self.active = False
        self.t0 = None
        self.event_id = None
        self._pre_needed = 0
        self._post_needed = 0
        self._post_got = 0

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
        else:  # "mp4" - 항상 '10초' 길이 유지(패딩 없이), 프레임수에 맞춰 FPS 조절
            duration_s = (self.pre_secs + self.post_secs)  # 10.0
            for cam in range(NUM_CAMS):
                items = cam_streams[cam]
                if not items:
                    continue

                # 첫 프레임에서 사이즈 얻기
                first = items[0][1]
                img0 = cv2.imdecode(np.frombuffer(first, dtype=np.uint8), cv2.IMREAD_COLOR)
                h, w = img0.shape[:2]

                # 프레임 개수에 맞춰 FPS 산정: N / duration_s
                N = len(items)
                fps_out = max(1.0, min(30.0, float(N) / max(0.001, duration_s)))  # 1~30 FPS 클램프

                vw = cv2.VideoWriter(str(Path(event_dir) / f"cam{cam}.mp4"),
                                    self.fourcc,
                                    fps_out,
                                    (w, h))
                if not vw.isOpened():
                    raise RuntimeError("VideoWriter open failed")

                # 패딩/중복 없이 그냥 있는 프레임만 순서대로 기록
                for _, jb in items:
                    fr = cv2.imdecode(np.frombuffer(jb, dtype=np.uint8), cv2.IMREAD_COLOR)
                    vw.write(fr)
                vw.release()

            print(f"[SAVE] {event_dir} frames_total={len(window)} "
                f"→ fixed_duration={duration_s:.2f}s (variable FPS)")