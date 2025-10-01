#!/usr/bin/env python3
# -*- coding: utf-8 -*-

'''
    calra + gstreaming + can통신

   python3 fast_hero_sender.py \
  --town Town03 --follow \
  --hero-speed 160 --hero-dist 1.0 --hero-lane-change \
  --num-traffic 50 \
  --serial /dev/ttyACM0 --baud 115200 \
  --pi-ip 10.10.14.33 --base-port 5000
    
    - hero-speed 160 : 차량 제한 속도의 160%로 주행
    - num-traffic 50 : 기본 배경자 수 50대
    - hero-dist 1.0 : 앞차와의 목표 거리(m), 작을수록 과감 (기본 1.0m)
    - hero-lane-change : 차선 변경 허용(기본허용)
'''

import sys, os, glob, time, math, argparse, random, struct
import numpy as np
import cv2

# ===== Optional: Serial =====
try:
    import serial
except Exception:
    serial = None

# ===== Optional: python-can (CAN 송신) =====
try:
    import can
except Exception:
    can = None

# ===== GStreamer / GI =====
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst

# ===== CARLA egg path (if needed) =====
try:
    sys.path.append(
        glob.glob(os.path.join('..', 'carla', 'dist',
            f"carla-*{sys.version_info.major}.{sys.version_info.minor}-linux-x86_64.egg"))[0]
    )
except Exception:
    pass

import carla

# =========================
# Config (기본값)
# =========================
PI_IP = "10.10.14.33"
WIDTH, HEIGHT, FPS = 800, 320, 15  # 각 카메라 해상도/프레임

# GStreamer 초기화 (전역 1회)
Gst.init(None)

# =========================
# 프로토콜 (히어로 차량 상태 V1)
# =========================
STX0, STX1 = 0xAA, 0x55
MSG_VEH_STATUS = 0x01  # <HhBBBB> (speed0.1, steer0.1, thr, brk, gear, flags)

def crc8_xor(bs: bytes) -> int:
    c = 0
    for b in bs: c ^= b
    return c & 0xFF

def pack_payload(speed01: int, steer01: int, thr: int, brk: int, gear: int, flags: int) -> bytes:
    return struct.pack('<HhBBBB', speed01, steer01, thr, brk, gear, flags)

def build_frame(payload: bytes) -> bytes:
    head = bytes([STX0, STX1, MSG_VEH_STATUS, len(payload)])
    return head + payload + bytes([crc8_xor(head + payload)])

def kph(v: carla.Vector3D) -> float:
    return 3.6 * math.sqrt(v.x*v.x + v.y*v.y + v.z*v.z)

# =========================
# 카메라 매니저 (히어로용 6뷰)
# =========================
class CarlaCameraManager:
    def __init__(self, world, vehicle, width=WIDTH, height=HEIGHT, fps=FPS):
        self.world = world
        self.vehicle = vehicle
        self.width = width
        self.height = height
        self.fps = fps
        self.cameras = []
        self.latest_frames = {}

    def spawn_cameras(self):
        bp_lib = self.world.get_blueprint_library()
        cam_bp = bp_lib.find("sensor.camera.rgb")
        cam_bp.set_attribute("image_size_x", str(self.width))
        cam_bp.set_attribute("image_size_y", str(self.height))
        cam_bp.set_attribute("sensor_tick", str(1.0 / self.fps))

        positions = [
            ( 2.5 ,  0.0, 0.7, 0,    0,   0),  # front (12시) - FOV 70
            ( 2.5 ,  0.5, 0.7, 0,   30,   0),  # 2시
            ( 2.5 , -0.5, 0.7, 0,  -30,   0),  # 10시
            (-2.5 ,  0.0, 0.7, 0,  180,   0),  # rear (6시)
            (-2.5 ,  0.5, 0.7, 0,  150,   0),  # 4시
            (-2.5 , -0.5, 0.7, 0, -150,   0),  # 8시
        ]

        for i, (x, y, z, pitch, yaw, roll) in enumerate(positions):
            transform = carla.Transform(
                carla.Location(x=x, y=y, z=z),
                carla.Rotation(pitch=pitch, yaw=yaw, roll=roll)
            )
            fov = "70" if i == 0 else "110"
            cam_bp.set_attribute("fov", fov)
            camera = self.world.spawn_actor(cam_bp, transform, attach_to=self.vehicle)

            def _cb(image, cam_id=i):
                if not self.cameras[cam_id].is_alive:
                    return
                arr = np.frombuffer(image.raw_data, dtype=np.uint8).reshape((self.height, self.width, 4))
                frame = arr[:, :, :3].copy()
                self.latest_frames[cam_id] = frame
                del image, arr, frame

            camera.listen(_cb)
            self.cameras.append(camera)

    def destroy(self):
        for cam in self.cameras:
            try:
                if cam.is_alive:
                    cam.stop()
                    cam.destroy()
            except:
                pass
        self.cameras.clear()
        self.latest_frames.clear()

# =========================
# GStreamer 파이프라인
# =========================
def make_pipeline(port, ip=PI_IP, width=WIDTH, height=HEIGHT, fps=FPS):
    pipeline_str = (
        "appsrc name=mysrc is-live=true block=true format=time "
        f"caps=video/x-raw,format=BGR,width={width},height={height},framerate={fps}/1 ! "
        "queue max-size-buffers=1 leaky=downstream ! "
        "videoconvert ! "
        f"video/x-raw,format=I420,width={width},height={height},framerate={fps}/1 ! "
        "x264enc tune=zerolatency bitrate=800 speed-preset=superfast key-int-max=30 ! "
        "rtph264pay config-interval=1 pt=96 ! "
        f"udpsink host={ip} port={port} sync=false async=false"
    )
    pipe = Gst.parse_launch(pipeline_str)
    appsrc = pipe.get_by_name("mysrc")

    caps = Gst.Caps.from_string(
        f"video/x-raw,format=BGR,width={width},height={height},framerate={fps}/1"
    )
    appsrc.set_property("caps", caps)
    appsrc.set_property("is-live", True)
    appsrc.set_property("block", True)
    appsrc.set_property("do-timestamp", True)
    appsrc.set_property("max-bytes", width * height * 3)

    pipe.set_state(Gst.State.PLAYING)
    return pipe, appsrc

def push_frame_to_appsrc(frame: np.ndarray, appsrc, frame_id=[0]):
    data = frame.tobytes()
    buf = Gst.Buffer.new_wrapped(data)
    frame_id[0] += 1
    ts = frame_id[0] * Gst.SECOND // FPS
    buf.pts = ts
    buf.dts = ts
    buf.duration = Gst.SECOND // FPS
    ret = appsrc.emit("push-buffer", buf)
    if ret != Gst.FlowReturn.OK:
        print(f"⚠️ push-buffer failed: {ret}")
    del data, buf, ts, ret

# =========================
# 스폰 유틸
# =========================
def spawn_hero(world: carla.World, bp_filter='vehicle.*model3*', color='255,0,0'):
    bp_lib = world.get_blueprint_library()
    cand = bp_lib.filter(bp_filter)
    bp = cand[0] if cand else bp_lib.filter('vehicle.*')[0]
    bp.set_attribute('role_name', 'hero')
    if bp.has_attribute('color'):
        bp.set_attribute('color', color)
    for sp in world.get_map().get_spawn_points():
        a = world.try_spawn_actor(bp, sp)
        if a:
            return a
    return None

def spawn_traffic(world: carla.World, n: int, seed: int = 42):
    if n <= 0:
        return []
    random.seed(seed)
    bp_lib = world.get_blueprint_library()
    veh_bps = bp_lib.filter('vehicle.*')
    sps = world.get_map().get_spawn_points()
    random.shuffle(sps)
    spawned = []
    for sp in sps:
        if len(spawned) >= n:
            break
        bp = random.choice(veh_bps)
        if bp.has_attribute('driver_id'):
            bp.set_attribute('driver_id', random.choice(bp.get_attribute('driver_id').recommended_values))
        a = world.try_spawn_actor(bp, sp)
        if a:
            spawned.append(a)
    return spawned

# =========================
# 메인
# =========================
def main():
    ap = argparse.ArgumentParser(description="CARLA 6뷰 스트리밍 + 히어로 고속 주행(속도 유지) + 히어로 상태 송신")
    # CARLA 접속/월드
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=2000)
    ap.add_argument('--town', default='', help='예) Town03 (빈값이면 현재 월드 유지)')
    ap.add_argument('--hz', type=float, default=float(FPS), help='CARLA tick 주기(Hz)')

    # 히어로/트래픽
    ap.add_argument('--hero-filter', default='vehicle.*model3*')
    ap.add_argument('--hero-color', default='255,0,0')

    # 🚀 히어로 목표 속도 (% of speed limit). 150 = 제한속도 150%
    ap.add_argument('--hero-speed', type=float, default=150.0)
    # 🔧 히어로 추종 거리(m): 작을수록 앞차에 덜 막힘
    ap.add_argument('--hero-dist', type=float, default=1.0)
    # 🔀 히어로 차선 변경 허용
    ap.add_argument('--hero-lane-change', action='store_true', default=True)

    # 배경차 대수 (기본 50대)
    ap.add_argument('--num-traffic', type=int, default=50, help='배경차 대수(히어로 제외)')

    # 배경차 감속(%) 필요시만 (0이면 제한속도 그대로)
    ap.add_argument('--traffic-slowdown', type=float, default=0.0)

    ap.add_argument('--destroy-others', action='store_true')
    ap.add_argument('--follow', action='store_true', help='간단 3인칭 추적 카메라')

    # GStreamer/네트워크
    ap.add_argument('--pi-ip', default=PI_IP)
    ap.add_argument('--base-port', type=int, default=5000)

    # 시리얼
    ap.add_argument('--serial', default='', help='/dev/ttyACM0 (비우면 전송X)')
    ap.add_argument('--baud', type=int, default=115200)

    # CAN 옵션 (히어로만 전송)
    ap.add_argument('--can', action='store_true', help='CAN 송신 활성화(socketcan)')
    ap.add_argument('--can-channel', default='can0', help='예: can0 / vcan0')
    ap.add_argument('--can-id', type=lambda x: int(x, 0), default=0x100, help='히어로 V1용 0x100 (0x.. 허용)')

    args = ap.parse_args()

    # ===== Serial open (optional)
    ser = None
    if args.serial:
        if serial is None:
            print("[WARN] pyserial not installed; serial disabled.")
        else:
            try:
                ser = serial.Serial(args.serial, args.baud, timeout=0.02)
                try:
                    ser.dtr = False; time.sleep(0.2)
                    ser.reset_input_buffer(); ser.reset_output_buffer()
                    ser.dtr = True
                except Exception:
                    pass
                time.sleep(2.0)
                print(f"[INFO] Serial open: {args.serial} @ {args.baud}")
            except Exception as e:
                print(f"[WARN] Serial open failed: {e}; continue without serial")
                ser = None

    # ===== CAN open (optional)
    bus = None
    if args.can:
        if can is None:
            print("[WARN] python-can not installed; CAN disabled.")
        else:
            try:
                bus = can.Bus(interface="socketcan", channel=args.can_channel)
                print(f"[INFO] CAN bus open: interface=socketcan channel={args.can_channel} id=0x{args.can_id:03X}")
            except Exception as e:
                print(f"[WARN] CAN open failed ({type(e).__name__}): {e}")
                bus = None

    # ===== CARLA 접속/설정
    client = carla.Client(args.host, args.port); client.set_timeout(10.0)
    world = client.load_world(args.town) if args.town else client.get_world()
    tm = client.get_trafficmanager()

    original = world.get_settings()
    new = world.get_settings()
    new.synchronous_mode = True
    new.fixed_delta_seconds = 1.0 / args.hz
    world.apply_settings(new)
    tm.set_synchronous_mode(True)
    world.tick()

    # ===== 사전 정리(옵션)
    if args.destroy_others:
        for a in world.get_actors().filter('vehicle.*'):
            try: a.destroy()
            except: pass
        world.tick()

    # ===== 히어로/트래픽 스폰
    hero = spawn_hero(world, args.hero_filter, args.hero_color)
    if hero is None:
        vs = world.get_actors().filter('vehicle.*')
        if vs:
            hero = vs[0]
            print(f"[WARN] Hero spawn failed → using existing vehicle id={hero.id}")
        else:
            print("[ERR] No vehicle available.")
            try: tm.set_synchronous_mode(False)
            except: pass
            world.apply_settings(original)
            if ser: 
                try: ser.close()
                except: pass
            if bus:
                try: bus.shutdown()
                except: pass
            return

    traffic = spawn_traffic(world, args.num_traffic)
    world.tick()

    # ===== Traffic Manager 설정 =====
    # 히어로 목표속도(%): TM은 "speed difference(%)"를 받으므로 diff = 100 - hero_speed
    hero_speed = max(10.0, min(300.0, float(args.hero_speed)))  # 10%~300% 가드
    diff_hero = 100.0 - hero_speed   # 150% → -50 (더 빠르게)

    # 히어로: 신호/표지 준수(원하면 100으로 바꿔 무시 가능)
    tm.ignore_lights_percentage(hero, 0)
    tm.ignore_signs_percentage(hero, 0)

    # 히어로: 목표 속도/차간거리/차선변경
    tm.vehicle_percentage_speed_difference(hero, diff_hero)
    try:
        tm.set_distance_to_leading_vehicle(hero, max(0.5, float(args.hero_dist)))  # m
    except Exception:
        # 일부 버전에선 개별 API 없고 글로벌만 있을 수 있어요
        try:
            tm.set_global_distance_to_leading_vehicle(max(0.5, float(args.hero_dist)))
        except Exception:
            pass
    try:
        tm.auto_lane_change(hero, bool(args.hero_lane_change))
    except Exception:
        pass

    hero.set_autopilot(True, tm.get_port())

    # 배경차: 필요시 감속만 적용(기본 0%)
    for v in traffic:
        try:
            tm.ignore_lights_percentage(v, 0)
            tm.ignore_signs_percentage(v, 0)
            if args.traffic_slowdown != 0.0:
                tm.vehicle_percentage_speed_difference(v, float(args.traffic_slowdown))
            v.set_autopilot(True, tm.get_port())
        except:
            pass

    print(f"[INFO] HERO id={hero.id} target={hero_speed:.0f}% of speed limit (TM diff={diff_hero:+.0f}%)")
    print(f"[INFO] traffic_spawned={len(traffic)} (requested {args.num_traffic})  hz={args.hz}")

    # ===== 히어로 6뷰 + RTP 송신 준비
    cam_manager = CarlaCameraManager(world, hero, width=WIDTH, height=HEIGHT, fps=FPS)
    cam_manager.spawn_cameras()

    pipelines, appsrcs = [], []
    for i in range(6):
        pipe, src = make_pipeline(args.base_port + i, ip=args.pi_ip, width=WIDTH, height=HEIGHT, fps=FPS)
        pipelines.append(pipe); appsrcs.append(src)

    try:
        while True:
            world.tick()

            # (옵션) 3인칭 추적 카메라
            if args.follow:
                tf = hero.get_transform()
                yaw = tf.rotation.yaw
                rad = math.radians(yaw)
                back = carla.Location(x=-8.0*math.cos(rad), y=-8.0*math.sin(rad), z=3.0)
                world.get_spectator().set_transform(
                    carla.Transform(tf.location + back, carla.Rotation(pitch=-10.0, yaw=yaw))
                )

            # --- 히어로 상태 수집 & 로그/송신 ---
            vel = hero.get_velocity()
            ctrl = hero.get_control()

            spd = kph(vel)
            steer_deg = float(ctrl.steer) * 30.0
            thr = int(max(0.0, min(1.0, ctrl.throttle)) * 100)
            brk = int(max(0.0, min(1.0, ctrl.brake)) * 100)
            gear_map = {0:0, 1:1, -1:2}
            gear = gear_map.get(ctrl.gear, 0)
            flags = 1 if getattr(hero, 'is_autopilot_enabled', False) else 0

            print(f"SPD={spd:6.2f} kph  STR={steer_deg:6.2f} deg  THR={thr:3d}%  BRK={brk:3d}%  G={gear}  FLG=0x{flags:02X}")

            # 시리얼 전송 (히어로만, V1)
            if ser:
                speed01 = int(round(spd * 10))
                steer01 = int(round(steer_deg * 10))
                payload = pack_payload(speed01, steer01, thr, brk, gear, flags)
                frame_bytes = build_frame(payload)
                try:
                    ser.write(frame_bytes)
                    ser.flush()
                except Exception as e:
                    print(f"[WARN] Serial write failed: {e}")

            # CAN 전송 (히어로만)
            if bus:
                try:
                    speed01 = int(round(spd * 10))
                    steer01 = int(round(steer_deg * 10))
                    payload = pack_payload(speed01, steer01, thr, brk, gear, flags)
                    msg = can.Message(arbitration_id=args.can_id, data=payload, is_extended_id=False)  # type: ignore
                    bus.send(msg)  # type: ignore
                except Exception as e:
                    print(f"[WARN] CAN send failed: {e}")

            # --- 6뷰 프레임 송출 + 로컬 미리보기 ---
            frames = cam_manager.latest_frames
            if len(frames) == 6:
                for i in range(6):
                    f = frames.get(i, None)
                    if f is not None:
                        push_frame_to_appsrc(f, appsrcs[i])
                try:
                    row1 = np.hstack([frames[0], frames[1], frames[2]])
                    row2 = np.hstack([frames[3], frames[4], frames[5]])
                    grid = np.vstack([row1, row2])
                    cv2.imshow("CARLA Preview (6-view)", grid)
                    if cv2.waitKey(1) == 27:
                        break
                except Exception:
                    pass
            del frames

    except KeyboardInterrupt:
        print("\n[INFO] Stopped by user.")
    finally:
        # 의미상 EOS
        for s in appsrcs:
            try: s.end_of_stream()
            except: pass
        time.sleep(0.1)

        for p in pipelines:
            try: p.set_state(Gst.State.NULL)
            except: pass

        cam_manager.destroy()

        try:
            hero.set_autopilot(False)
        except: pass
        for v in traffic:
            try: v.set_autopilot(False)
            except: pass
        try:
            hero.destroy()
        except: pass
        for v in traffic:
            try: v.destroy()
            except: pass

        if ser:
            try: ser.close()
            except: pass

        if bus:
            try: bus.shutdown()
            except: pass

        try:
            tm.set_synchronous_mode(False)
        except: pass
        world.apply_settings(original)
        cv2.destroyAllWindows()
        print("[INFO] Cleaned up.")

if __name__ == "__main__":
    main()
