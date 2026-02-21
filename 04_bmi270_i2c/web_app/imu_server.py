#!/usr/bin/env python3
import os
import time
import math
import struct
import json
import asyncio
import threading
import numpy as np
import websockets

from ahrs.filters import Mahony
from ahrs.common.orientation import q2euler

# =============================
# 配置与常量
# =============================
DEV_NODE = "/dev/iio:device0"
BASE_DIR = "/sys/bus/iio/devices/iio:device0"
WS_PORT = 8765
WS_HOST = "0.0.0.0"

# 全局共享状态
class SharedState:
    def __init__(self):
        self.lock = threading.Lock()
        self.running = True
        
        self.data = {
            "q": [1.0, 0.0, 0.0, 0.0],  # w, x, y, z
            "euler": [0.0, 0.0, 0.0],   # Roll, Pitch, Yaw
            "acc": [0.0, 0.0, 0.0],
            "gyr": [0.0, 0.0, 0.0],
            "ts": 0.0,
            "fps": 0.0,
            "trust": 0.0,
            "stationary": False
        }
        
        # Mahony 参数：这里 ki 改成了 0.001 避免报错
        self.params = {
            "kp": 0.5,           
            "ki": 0.001,         
            "gyro_thresh": 0.02,
            "acc_g_thresh": 0.06,
            "bias_alpha": 0.002,
            "use_dyn_kp": True   
        }

state = SharedState()

# =============================
# IIO 辅助函数
# =============================
def read_text(path, default=""):
    try:
        with open(path, "r") as f:
            return f.read().strip()
    except Exception:
        return default

def read_float(path, default=None):
    s = read_text(path, "")
    try: return float(s)
    except Exception: return default

def read_int(path, default=None):
    s = read_text(path, "")
    try: return int(s)
    except Exception: return default

def se_path(name):
    return os.path.join(BASE_DIR, "scan_elements", name)

def read_type(name: str) -> str:
    return read_text(se_path(f"{name}_type"), "")

def quat_from_accel(ax, ay, az):
    roll = math.atan2(ay, az)
    pitch = math.atan2(-ax, math.sqrt(ay * ay + az * az))
    cy, sy = math.cos(0.0), math.sin(0.0)
    cp, sp = math.cos(pitch * 0.5), math.sin(pitch * 0.5)
    cr, sr = math.cos(roll * 0.5), math.sin(roll * 0.5)
    w = cr * cp * cy + sr * sp * sy
    x = sr * cp * cy - cr * sp * sy
    y = cr * sp * cy + sr * cp * sy
    z = cr * cp * sy - sr * sp * cy
    q = np.array([w, x, y, z], dtype=float)
    n = np.linalg.norm(q)
    return q / n if n > 0 else np.array([1.0, 0.0, 0.0, 0.0])

def is_stationary(gyr_rad_s, acc_m_s2, g=9.80665, gyro_thresh=0.02, acc_g_thresh=0.06):
    gnorm = np.linalg.norm(acc_m_s2)
    if gnorm < 1e-6: return False
    acc_dev_g = abs(gnorm - g) / g
    return (np.linalg.norm(gyr_rad_s) < gyro_thresh) and (acc_dev_g < acc_g_thresh)

def parse_iio_type(type_str: str):
    try:
        parts = type_str.split(":")
        endian_s = parts[0]
        rest = parts[1]
        signed_s, bits_part = rest[0], rest[1:]
        signed = (signed_s == "s")
        endian = "<" if endian_s == "le" else ">"
        left = bits_part.split(">>")[0]
        realbits_s, storagebits_s = left.split("/")
        return endian, signed, int(realbits_s), int(storagebits_s)
    except Exception:
        return None

def build_frame_parser():
    idx = {
        "ax": read_int(se_path("in_accel_x_index")), "ay": read_int(se_path("in_accel_y_index")), "az": read_int(se_path("in_accel_z_index")),
        "gx": read_int(se_path("in_anglvel_x_index")), "gy": read_int(se_path("in_anglvel_y_index")), "gz": read_int(se_path("in_anglvel_z_index")),
        "ts": read_int(se_path("in_timestamp_index")),
    }
    t = {
        "ax": read_type("in_accel_x"), "ay": read_type("in_accel_y"), "az": read_type("in_accel_z"),
        "gx": read_type("in_anglvel_x"), "gy": read_type("in_anglvel_y"), "gz": read_type("in_anglvel_z"),
        "ts": read_type("in_timestamp"),
    }
    tp = {k: parse_iio_type(v) for k, v in t.items()}
    
    items = []
    for name in ["ax", "ay", "az", "gx", "gy", "gz", "ts"]:
        endian, signed, _, storagebits = tp[name]
        items.append((name, storagebits, endian, signed))
    items.sort(key=lambda x: idx[x[0]])

    offset = 0
    offsets = {}
    for name, storagebits, endian, signed in items:
        align = storagebits // 8
        if align > 1 and (offset % align) != 0:
            offset += (align - (offset % align))
        offsets[name] = offset
        offset += storagebits // 8
    frame_size = offset

    def unpack_val(buf, name):
        storagebits, endian, signed = tp[name][3], tp[name][0], tp[name][1]
        if storagebits == 16: fmt = endian + ("h" if signed else "H")
        elif storagebits == 32: fmt = endian + ("i" if signed else "I")
        elif storagebits == 64: fmt = endian + ("q" if signed else "Q")
        else: raise RuntimeError(f"unsupported storagebits={storagebits}")
        return struct.unpack_from(fmt, buf, offsets[name])[0]

    def parse_frame(buf):
        return (unpack_val(buf, "ax"), unpack_val(buf, "ay"), unpack_val(buf, "az"),
                unpack_val(buf, "gx"), unpack_val(buf, "gy"), unpack_val(buf, "gz"),
                unpack_val(buf, "ts"))

    return parse_frame, frame_size

# =============================
# 核心解算线程
# =============================
def imu_processing_thread():
    print("[THREAD] IMU Processing started (Mahony Filter)...")
    
    accel_scale = read_float(os.path.join(BASE_DIR, "in_accel_scale"), 0.0)
    gyro_scale  = read_float(os.path.join(BASE_DIR, "in_anglvel_scale"), 0.0)
    
    parse_frame, frame_size = build_frame_parser()
    fd = os.open(DEV_NODE, os.O_RDONLY)
    
    # Flush buffer
    t_flush = time.time() + 0.5
    while time.time() < t_flush:
        try: os.read(fd, frame_size)
        except: pass

    print("[THREAD] Initializing bias...")
    acc_list, gyr_list = [], []
    t_end = time.time() + 1.0
    while time.time() < t_end:
        buf = os.read(fd, frame_size)
        if len(buf) != frame_size: continue
        ax, ay, az, gx, gy, gz, ts = parse_frame(buf)
        acc_list.append([ax, ay, az])
        gyr_list.append([gx, gy, gz])
    
    acc_mean = np.mean(np.array(acc_list), axis=0) * accel_scale
    gyr_mean = np.mean(np.array(gyr_list), axis=0) * gyro_scale
    
    gyro_bias = gyr_mean
    if np.linalg.norm(gyro_bias) > 0.1: 
        gyro_bias = np.zeros(3)

    q = quat_from_accel(acc_mean[0], acc_mean[1], acc_mean[2])
    
    # 初始化 Mahony 滤波器：k_I 改为 0.001 避免报错
    filter_algo = Mahony(sampleperiod=0.01, k_P=0.5, k_I=0.001, q0=q)
    
    last_ts = None
    g0 = 9.80665
    acc_sigma = 0.15
    stationary_cnt = 0
    stationary_hold = 30
    
    loop_count = 0
    t_fps_start = time.time()

    try:
        while state.running:
            buf = os.read(fd, frame_size)
            if len(buf) != frame_size: continue

            axr, ayr, azr, gxr, gyr, gzr, ts = parse_frame(buf)
            acc = np.array([axr, ayr, azr], dtype=float) * accel_scale
            gyr_v = np.array([gxr, gyr, gzr], dtype=float) * gyro_scale * 0.5

            # DT 计算
            if last_ts is None:
                dt = 0.01
            else:
                dt = (ts - last_ts) / 1e9
                if dt <= 0 or dt > 0.1: dt = 0.01
            last_ts = ts
            
            filter_algo.sampleperiod = dt

            params = state.params
            gyr_u = gyr_v - gyro_bias

            # 动态信任度计算
            acc_norm = np.linalg.norm(acc)
            acc_trust = 0.0
            if acc_norm > 1e-6:
                acc_dev = abs(acc_norm - g0) / g0
                acc_trust = math.exp(- (acc_dev / acc_sigma) ** 2)
            
            # 设置 Mahony 参数
            current_kp = params["kp"]
            if params["use_dyn_kp"]:
                # 当有剧烈线加速度时(信任度低)，自动降低 kp，防止画面乱飞/过冲
                current_kp = current_kp * (0.1 + 0.9 * acc_trust)
            
            filter_algo.k_P = current_kp
            filter_algo.k_I = params["ki"]

            # 静止检测 & Bias 更新
            is_stat = is_stationary(gyr_u, acc, g=g0, 
                                    gyro_thresh=params["gyro_thresh"], 
                                    acc_g_thresh=params["acc_g_thresh"])
            
            if is_stat: stationary_cnt += 1
            else: stationary_cnt = 0

            if stationary_cnt >= stationary_hold:
                gyro_bias = (1.0 - params["bias_alpha"]) * gyro_bias + params["bias_alpha"] * gyr_v

            # AHRS 更新 (使用 Mahony)
            q = filter_algo.updateIMU(q, gyr=gyr_u, acc=acc)
            
            loop_count += 1
            if loop_count % 5 == 0: 
                r, p, y = q2euler(q)
                
                now = time.time()
                
                with state.lock:
                    # 只有时间大于1秒时，才更新计算 FPS，否则保留上一秒的值
                    if now - t_fps_start >= 1.0:
                        state.data["fps"] = loop_count / (now - t_fps_start)
                        loop_count = 0
                        t_fps_start = now

                    # 正常更新其他数据
                    state.data["q"] = q.tolist()
                    state.data["euler"] = [math.degrees(r), math.degrees(p), math.degrees(y)]
                    state.data["acc"] = acc.tolist()
                    state.data["gyr"] = gyr_u.tolist()
                    state.data["ts"] = ts
                    state.data["trust"] = acc_trust
                    state.data["stationary"] = (stationary_cnt >= stationary_hold)

    except Exception as e:
        print(f"[ERR] Thread crash: {e}")
    finally:
        os.close(fd)

# =============================
# WebSocket 服务端
# =============================
async def handler(websocket):
    print(f"[WS] Client connected: {websocket.remote_address}")
    async def receiver():
        try:
            async for message in websocket:
                try:
                    cmd = json.loads(message)
                    if cmd.get("type") == "params":
                        payload = cmd.get("payload", {})
                        for k, v in payload.items():
                            if k in state.params:
                                state.params[k] = float(v) if k != "use_dyn_kp" else bool(v)
                except Exception as e:
                    pass
        except websockets.exceptions.ConnectionClosed:
            pass

    async def sender():
        try:
            while True:
                with state.lock:
                    packet = state.data.copy()
                msg = { "type": "imu_update", "payload": packet }
                await websocket.send(json.dumps(msg))
                await asyncio.sleep(0.033)
        except websockets.exceptions.ConnectionClosed:
            pass

    await asyncio.gather(receiver(), sender())
    print(f"[WS] Client disconnected: {websocket.remote_address}")

async def main():
    t = threading.Thread(target=imu_processing_thread, daemon=True)
    t.start()
    print(f"Starting WebSocket Server on ws://{WS_HOST}:{WS_PORT}")
    async with websockets.serve(handler, WS_HOST, WS_PORT):
        await asyncio.Future()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopping...")
        state.running = False