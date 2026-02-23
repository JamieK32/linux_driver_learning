#!/usr/bin/env python3
import os
import time
import math
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
            "stationary": False,
            "moving": False             # 明确标识是否正在移动
        }
        
        # Mahony 参数
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
# 辅助函数
# =============================
def auto_cleanup_iio_state():
    """程序自己接管并清理底层的 IIO Buffer 和 Trigger 占用状态"""
    print("[INIT] Automatically cleaning up leftover IIO states...")
    try:
        # 1. 确保关闭所有的 Buffer 模式
        for buf_name in ["buffer", "buffer0"]:
            buf_en_path = os.path.join(BASE_DIR, buf_name, "enable")
            if os.path.exists(buf_en_path):
                with open(buf_en_path, "w") as f:
                    f.write("0\n")
                    
        # 2. 确保解绑 Trigger（清空 current_trigger）
        trig_path = os.path.join(BASE_DIR, "trigger", "current_trigger")
        if os.path.exists(trig_path):
            with open(trig_path, "w") as f:
                f.write("\n")
                
        print("[INIT] IIO Buffer & Trigger cleared successfully. Ready for Direct Read.")
    except PermissionError:
        print("[WARN] Permission denied when cleaning IIO state. Try running with 'sudo' if sensor is blocked.")
    except Exception as e:
        print(f"[WARN] Minor issue during IIO cleanup: {e}")

def read_float_once(path, default=0.0):
    try:
        with open(path, "r") as f:
            return float(f.read().strip())
    except Exception:
        return default

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

# =============================
# 核心解算线程 (Sysfs 直读版)
# =============================
def imu_processing_thread():
    # 程序启动第一件事：自己清理状态
    auto_cleanup_iio_state()
    
    print("[THREAD] IMU Processing started (Sysfs Direct Read)...")
    
    # 获取量程 Scale
    accel_scale = read_float_once(os.path.join(BASE_DIR, "in_accel_scale"), 0.0)
    gyro_scale  = read_float_once(os.path.join(BASE_DIR, "in_anglvel_scale"), 0.0)
    
    if accel_scale == 0.0 or gyro_scale == 0.0:
        print("[WARN] Scale is 0.0, please check if device is properly loaded.")
    
    # 打开所有 raw 文件的句柄
    try:
        fds = {
            "ax": open(os.path.join(BASE_DIR, "in_accel_x_raw"), "r"),
            "ay": open(os.path.join(BASE_DIR, "in_accel_y_raw"), "r"),
            "az": open(os.path.join(BASE_DIR, "in_accel_z_raw"), "r"),
            "gx": open(os.path.join(BASE_DIR, "in_anglvel_x_raw"), "r"),
            "gy": open(os.path.join(BASE_DIR, "in_anglvel_y_raw"), "r"),
            "gz": open(os.path.join(BASE_DIR, "in_anglvel_z_raw"), "r"),
        }
    except Exception as e:
        print(f"[ERR] Failed to open sysfs nodes: {e}")
        return

    def read_raw(fd):
        fd.seek(0)
        try:
            return float(fd.read().strip())
        except ValueError:
            return 0.0

    print("[THREAD] Initializing bias...")
    acc_list, gyr_list = [], []
    t_end = time.time() + 1.0
    while time.time() < t_end and state.running:
        ax = read_raw(fds["ax"])
        ay = read_raw(fds["ay"])
        az = read_raw(fds["az"])
        gx = read_raw(fds["gx"])
        gy = read_raw(fds["gy"])
        gz = read_raw(fds["gz"])
        
        acc_list.append([ax, ay, az])
        gyr_list.append([gx, gy, gz])
        time.sleep(0.01)
    
    if not state.running:
        return

    acc_mean = np.mean(np.array(acc_list), axis=0) * accel_scale
    gyr_mean = np.mean(np.array(gyr_list), axis=0) * gyro_scale
    
    gyro_bias = gyr_mean
    if np.linalg.norm(gyro_bias) > 0.1: 
        gyro_bias = np.zeros(3)

    q = quat_from_accel(acc_mean[0], acc_mean[1], acc_mean[2])
    
    # 初始化 Mahony 滤波器
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
            # 高频直读原始数据
            axr = read_raw(fds["ax"])
            ayr = read_raw(fds["ay"])
            azr = read_raw(fds["az"])
            gxr = read_raw(fds["gx"])
            gyr = read_raw(fds["gy"])
            gzr = read_raw(fds["gz"])

            acc = np.array([axr, ayr, azr], dtype=float) * accel_scale
            gyr_v = np.array([gxr, gyr, gzr], dtype=float) * gyro_scale * 0.5

            current_time = time.perf_counter()
            if last_ts is None:
                dt = 0.01
            else:
                dt = current_time - last_ts
                if dt <= 0 or dt > 0.1: 
                    dt = 0.01
            last_ts = current_time
            
            filter_algo.sampleperiod = dt
            params = state.params
            gyr_u = gyr_v - gyro_bias

            acc_norm = np.linalg.norm(acc)
            acc_trust = 0.0
            if acc_norm > 1e-6:
                acc_dev = abs(acc_norm - g0) / g0
                acc_trust = math.exp(- (acc_dev / acc_sigma) ** 2)
            
            current_kp = params["kp"]
            if params["use_dyn_kp"]:
                current_kp = current_kp * (0.1 + 0.9 * acc_trust)
            
            filter_algo.k_P = current_kp
            filter_algo.k_I = params["ki"]

            # 静止/运动状态检测
            is_stat = is_stationary(gyr_u, acc, g=g0, 
                                    gyro_thresh=params["gyro_thresh"], 
                                    acc_g_thresh=params["acc_g_thresh"])
            
            if is_stat: 
                stationary_cnt += 1
            else: 
                stationary_cnt = 0

            # 连续 30 帧静止，则判定为彻底静止并更新 Bias
            is_truly_stationary = (stationary_cnt >= stationary_hold)
            if is_truly_stationary:
                gyro_bias = (1.0 - params["bias_alpha"]) * gyro_bias + params["bias_alpha"] * gyr_v

            # AHRS 更新
            q = filter_algo.updateIMU(q, gyr=gyr_u, acc=acc)
            
            loop_count += 1
            if loop_count % 5 == 0: 
                r, p, y = q2euler(q)
                now = time.time()
                
                with state.lock:
                    if now - t_fps_start >= 1.0:
                        state.data["fps"] = loop_count / (now - t_fps_start)
                        loop_count = 0
                        t_fps_start = now

                    state.data["q"] = q.tolist()
                    state.data["euler"] = [math.degrees(r), math.degrees(p), math.degrees(y)]
                    state.data["acc"] = acc.tolist()
                    state.data["gyr"] = gyr_u.tolist()
                    state.data["ts"] = now * 1e9
                    state.data["trust"] = acc_trust
                    
                    # 写入运动状态供前端使用
                    state.data["stationary"] = is_truly_stationary
                    state.data["moving"] = not is_truly_stationary

    except Exception as e:
        print(f"[ERR] Thread crash: {e}")
    finally:
        for fd in fds.values():
            fd.close()

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
                except Exception:
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