# IMU Telemetry Dashboard | 企业级姿态可视化控制台

这是一个为 Linux IIO 设备（如 BMI270）设计的实时 3D 姿态可视化与算法调优 Web 控制台。采用 Python 后端解算 (Mahony 滤波) + WebSocket 实时传输 + Three.js 零延迟渲染。

![Dashboard Screenshot](./image.png) 

## 📦 依赖安装

在运行之前，请确保你的设备（如树莓派）上已安装以下 Python 库：

```bash
pip3 install numpy ahrs websockets

```

## 🚀 快速启动

本应用采用前后端分离架构，只需两步即可启动：

### 1. 启动数据解算后端

打开一个终端，运行核心解算与 WebSocket 服务端：

```bash
python3 imu_server.py

```

*(成功后会看到提示：`Starting WebSocket Server on ws://0.0.0.0:8765`)*

### 2. 启动前端网页界面

**新开一个终端窗口**（保持上一个终端不要关），在包含 `index.html` 的目录下启动一个简易 Web 服务器：

```bash
python3 -m http.server 8080

```

### 3. 访问控制台

打开你电脑的浏览器，在地址栏输入：
👉 `http://<你树莓派的局域网IP>:8080`
例如👉 `http://192.168.0.198:8080`

> **注意**：请使用 `8080` 端口访问网页，网页会自动在后台连接 `8765` 端口获取实时姿态数据。

## 🎮 常见问题排查

* **3D 模型转动方向与手部动作相反？**
无需修改代码，直接在网页右侧的 **Axis Calibration** 面板中，拨动对应的 `INV X/Y/Z` 开关即可瞬间修正。
* **画面出现过冲 (Overshoot)？**
在网页右侧的 **Mahony Tuning** 面板中，尝试微调 `Kp` (比例增益) 滑块，或者确保开启了 `Anti-Overshoot (Dyn Kp)` 防过冲开关。