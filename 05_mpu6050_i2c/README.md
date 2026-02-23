很好 👍 这个项目已经是一个**完整的 IIO 三轴 IMU 驱动雏形**了。
我给你写一份**结构清晰、适合复习的 README**，会包含：

* 架构说明
* IIO 设计思想
* 代码关键点解释
* 构建步骤
* 设备树说明
* 常见问题
* 复习重点总结

你可以直接保存为：

```
README.md
```

---

# 📘 MPU6050 IIO 驱动（Core + I2C）学习项目

---

# 一、项目目标

本项目实现：

✅ 基于 IIO 框架的 MPU6050 驱动
✅ Core + I2C 分离架构（类似 BMI270）
✅ 支持：

* 加速度三轴 raw
* 陀螺仪三轴 raw
* scale
* scale_available
* sampling_frequency
* sampling_frequency_available

当前模式：

```
INDIO_DIRECT_MODE
```

不包含 buffer 模式（后续可扩展）

---

# 二、项目目录结构

```
05_mpu6050_i2c/
│
├── Makefile
├── mpu6050.h
├── mpu6050_core.c
├── mpu6050_i2c.c
└── mpu6050-overlay.dts
```

---

# 三、驱动架构说明

## 1️⃣ Core + Bus 分离设计

模仿主线 BMI270 结构：

```
mpu6050_core.c   ← 纯 IIO + 硬件逻辑
mpu6050_i2c.c    ← I2C 适配层
```

### 优点

* 可扩展 SPI
* 可复用 core
* 结构清晰

---

## 2️⃣ 数据流结构

```
用户 cat sysfs
        ↓
IIO core
        ↓
read_raw()
        ↓
regmap
        ↓
I2C
        ↓
MPU6050
```

---

# 四、IIO 关键机制解析

---

## 1️⃣ Channel 定义

```c
static const struct iio_chan_spec mpu6050_channels[]
```

定义：

* accel x/y/z
* gyro x/y/z

### RAW 使用

```c
.info_mask_separate = BIT(IIO_CHAN_INFO_RAW)
```

因为：

> 每个轴是不同物理通道

生成：

```
in_accel_x_raw
in_accel_y_raw
in_accel_z_raw
```

---

### SCALE / SAMP_FREQ 使用 shared_by_type

```c
.info_mask_shared_by_type
```

因为：

> 三个轴共用同一个量程寄存器

生成：

```
in_accel_scale
in_anglvel_scale
```

而不是：

```
in_accel_x_scale
```

---

## 2️⃣ direct mode 机制

```c
indio_dev->modes = INDIO_DIRECT_MODE;
```

表示：

> 允许直接读取寄存器（非 buffer）

---

### 为什么 read_raw 需要：

```c
iio_device_claim_direct_mode()
```

原因：

* 防止与 buffer 模式冲突
* 防止寄存器访问竞争
* 保证 IIO 模式互斥

---

# 五、寄存器原理复习

---

## 1️⃣ SMPLRT_DIV

公式：

```
SampleRate = 1000 / (1 + SMPLRT_DIV)
```

例如：

| div | 采样率    |
| --- | ------ |
| 0   | 1000Hz |
| 9   | 100Hz  |
| 99  | 10Hz   |

---

## 2️⃣ FS_SEL 量程控制

位：

```
[4:3]
```

| 值 | accel | gyro      |
| - | ----- | --------- |
| 0 | ±2g   | ±250 dps  |
| 1 | ±4g   | ±500 dps  |
| 2 | ±8g   | ±1000 dps |
| 3 | ±16g  | ±2000 dps |

---

# 六、regmap 的作用

regmap 抽象了：

```
寄存器访问
缓存
位操作
总线无关性
```

你只写：

```c
regmap_read()
regmap_write()
regmap_update_bits()
```

不用关心 I2C 细节。

---

# 七、构建步骤

---

## 1️⃣ 编译

```bash
make
```

等价于：

```bash
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

生成：

```
mpu6050_core.ko
mpu6050_i2c.ko
```

---

## 2️⃣ 加载

```bash
sudo insmod mpu6050_core.ko
sudo insmod mpu6050_i2c.ko
```

---

## 3️⃣ 验证

```bash
ls /sys/bus/iio/devices/
cat /sys/bus/iio/devices/iio:device0/name
```

应该显示：

```
mpu6050
```

---

# 八、设备树 Overlay

文件：

```
mpu6050-overlay.dts
```

当前内容：

```dts
mpu6050@68 {
    compatible = "mycompany,mpu6050-minimal";
    reg = <0x68>;
};
```

⚠ 注意：

你的 i2c driver 匹配的是：

```c
.compatible = "invensense,mpu6050"
```

所以 overlay 应改为：

```dts
compatible = "invensense,mpu6050";
```

否则不会自动 probe。

---

## 编译 overlay

```bash
dtc -@ -I dts -O dtb -o mpu6050.dtbo mpu6050-overlay.dts
sudo cp mpu6050.dtbo /boot/firmware/overlays/
```

在 config.txt 添加：

```
dtoverlay=mpu6050
```

---

# 九、常见问题

---

## ❌ 没有 iio:device0

检查：

```
dmesg | grep mpu
```

可能原因：

* compatible 不匹配
* I2C 地址错误
* 模块未加载
* WHO_AM_I 不匹配

---

## ❌ 读 raw 出现 -EBUSY

说明：

```
buffer 已开启
```

direct mode 不允许同时访问。

---

# 十、当前驱动局限

当前版本：

```
✔ 直接模式
✔ scale
✔ sampling_frequency
```

不支持：

```
✘ buffer
✘ trigger
✘ timestamp
✘ FIFO
```

---

# 十一、下一步升级方向

建议升级路径：

1️⃣ 添加 scan_index + scan_type
2️⃣ 添加 triggered buffer
3️⃣ 使用 regmap_bulk_read
4️⃣ 加 timestamp
5️⃣ 支持 /dev/iio:deviceX

---

# 十二、复习重点总结（必须会）

如果面试问你，你要能回答：

* 为什么 scale 用 shared_by_type？
* 为什么 read_raw 要 claim_direct_mode？
* SMPLRT_DIV 为什么是 1 + div？
* 为什么 bulk_read 更好？
* direct mode 为什么不能保证三轴同时刻？

---