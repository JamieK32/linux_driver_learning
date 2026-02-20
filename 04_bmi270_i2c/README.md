# BMI270 移植到 Raspberry Pi (Linux 6.12) 完整工程指南

------

# 一、项目目标

本项目目标：

- 将 **主线 Linux 内核 BMI270 IIO 驱动**
  移植至 **Raspberry Pi 6.12 内核**

- 解决内核 API 版本差异问题

- 成功编译生成：

  ```
  bmi270.ko
  bmi270_i2c.ko
  ```

- 正确加载 `bmi270-init-data.fw` 固件

- 在 `/sys/bus/iio/devices/` 下成功读取：

  - 加速度
  - 陀螺仪
  - scale
  - raw 数据

------

# 二、整体移植流程概览

```
获取主线驱动源码
        ↓
合入 Raspberry Pi 内核树
        ↓
解决 6.12 API 差异
        ↓
编译模块
        ↓
部署固件
        ↓
添加 Device Tree Overlay
        ↓
验证 IIO 设备
        ↓
编写用户态读取程序
```

------

# 三、准备工作

------

## 1️⃣ 获取 BMI270 主线驱动源码

路径：

```
drivers/iio/imu/bmi270/
```

复制以下文件到你的工作目录：

```
/home/pi/linux_driver_learning/04_bmi270_i2c/bmi270
```

文件列表：

- bmi270_core.c
- bmi270_i2c.c
- bmi270_spi.c
- bmi270.h
- Kconfig
- Makefile

------

## 2️⃣ 拉取 Raspberry Pi 6.12 内核源码

```bash
sudo apt update
sudo apt install -y git bc bison flex libssl-dev make libncurses5-dev

mkdir -p ~/rpi
cd ~/rpi
git clone --depth=1 https://github.com/raspberrypi/linux.git
cd linux
```

------

# 四、将驱动合入内核树

------

## 1️⃣ 创建驱动目录

```bash
mkdir -p drivers/iio/imu/bmi270
cp -a ~/linux_driver_learning/04_bmi270_i2c/bmi270/* drivers/iio/imu/bmi270/
```

------

## 2️⃣ 修改 Kconfig

编辑：

```
drivers/iio/imu/Kconfig
```

追加：

```plaintext
source "drivers/iio/imu/bmi270/Kconfig"
```

------

## 3️⃣ 修改 Makefile

编辑：

```
drivers/iio/imu/Makefile
```

追加：

```make
obj-$(CONFIG_BMI270) += bmi270/
```

------

# 五、解决 Linux 6.12 API 兼容问题（核心）

由于主线驱动版本较新，与 Raspberry Pi 6.12 存在 API 差异，需要修改源码。

------

## 1️⃣ direct_mode API 变化

### ❌ 旧版本写法

```c
iio_device_claim_direct(indio_dev)
```

### ✅ 6.12 正确写法

```c
ret = iio_device_claim_direct_mode(indio_dev);
if (ret)
    return ret;
```

📌 修改示意图：

![img](images/code2.png)

------

### 原因分析

在新内核中：

```
iio_device_claim_direct()
```

已被替换为：

```
iio_device_claim_direct_mode()
```

并且需要检查返回值。

------

## 2️⃣ write_event_config 参数类型修改

### ❌ 原始版本

```c
bool state
```

### ✅ 修改为

```c
int state
```

📌 修改示意图：

![img](images/code1.png)

------

### 原因分析

IIO 子系统接口在 6.x 内核中统一改为 `int state`。

------

## 3️⃣ 移除 symbol namespace

### ❌ 原代码

```c
EXPORT_SYMBOL_NS_GPL(..., IIO_BMI270);
```

### ✅ 修改为

```c
EXPORT_SYMBOL_GPL(...);
```

📌 修改示意图：

![img](images/code3.png)

------

### 原因

Raspberry Pi 内核未启用 symbol namespace 支持。

------

# 六、内核配置与编译

------

## 1️⃣ 加载默认配置

```bash
cd ~/rpi/linux
make bcm2711_defconfig
```

------

## 2️⃣ 进入 menuconfig

```bash
make menuconfig
```

路径：

```
Device Drivers
    → Industrial I/O support
        → Inertial measurement units
```

![img](images/menu_config.png)

启用：

```
CONFIG_BMI270=m
CONFIG_BMI270_I2C=m
```

验证：

```bash
grep CONFIG_BMI270 .config
```

------

## 3️⃣ 编译

```bash
make -j$(nproc) modules
make -j$(nproc) Image modules dtbs
```

------

## 4️⃣ 安装

```bash
sudo make modules_install
sudo depmod -a

sudo cp arch/arm64/boot/Image /boot/firmware/kernel8.img
sudo cp arch/arm64/boot/dts/broadcom/*.dtb /boot/firmware/
sudo cp arch/arm64/boot/dts/overlays/*.dtb* /boot/firmware/overlays/
sudo reboot
```

------

# 七、部署 BMI270 初始化固件

------

## 为什么必须部署固件？

驱动 probe 时调用：

```c
request_firmware("bmi270-init-data.fw")
```

若缺失，将报错：

```
-ENOENT
```

并导致 probe 失败。

------

## 部署步骤

1️⃣ Windows 生成 `.fw` 文件
2️⃣ 通过 VSCode Remote-SSH 上传
3️⃣ 安装：

```bash
sudo cp bmi270-init-data.fw /lib/firmware/
sudo chmod 644 /lib/firmware/bmi270-init-data.fw
sync
```

参考文档：

```
./bmi270_firmware.md
```

------

# 八、Device Tree Overlay

------

## 1️⃣ mybmi270-overlay.dts

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target = <&i2c1>;
        __overlay__ {
            #address-cells = <1>;
            #size-cells = <0>;

            bmi270@69 {
                compatible = "bosch,bmi270";
                reg = <0x69>;

                interrupt-parent = <&gpio>;
                interrupts = <17 0x1>;
                interrupt-names = "INT1";

                drive-open-drain;
                status = "okay";
            };
        };
    };
};
```

------

## 2️⃣ 编译

```bash
dtc -@ -I dts -O dtb -o mybmi270.dtbo mybmi270-overlay.dts
sudo cp mybmi270.dtbo /boot/firmware/overlays/
```

------

## 3️⃣ 修改 /boot/firmware/config.txt

```
dtoverlay=mybmi270
```

重启。

------

# 九、硬件连接

| BMI270 | Raspberry Pi | Header |
| ------ | ------------ | ------ |
| INT1   | GPIO17       | Pin 11 |
| SDA    | GPIO2        | Pin 3  |
| SCL    | GPIO3        | Pin 5  |
| VCC    | 3.3V         | 1 / 17 |
| GND    | GND          | 6 / 9  |

![img](images/hardware.jpg)

验证地址：

```bash
sudo i2cdetect -y 1
```

![img](images/i2cdetect.png)

------

# 十、驱动验证

------

## 1️⃣ 查看 IIO 设备

```bash
ls /sys/bus/iio/devices/
```

------

## 2️⃣ 查看设备名

```bash
cat /sys/bus/iio/devices/iio:device0/name
```

期望输出：

```
bmi270
```

------

## 3️⃣ 查看 raw 通道

```bash
ls /sys/bus/iio/devices/iio:device0 | grep raw
```

------

## 4️⃣ 查看 scale

```bash
cat /sys/bus/iio/devices/iio:device0/in_accel_scale
```

示例：

```
0.002394
```

------

# 十一、用户态读取程序

![img](images/app.png)

编译：

```bash
gcc bmi270_read_sysfs.c -o bmi270_app
./bmi270_app
```

输出示例：

![img](images/bmi270_output.png)

------

# 十二、常见问题排查

------

### ❌ probe 失败

检查：

```
dmesg | grep bmi
```

若提示：

```
request_firmware failed
```

说明固件未正确安装。

------

### ❌ 无 iio 设备

检查：

```
lsmod | grep bmi
```

------

### ❌ I2C 未识别

```
sudo i2cdetect -y 1
```

若无 0x69：

- 检查接线
- 检查电压
- 检查地址跳线

------

# 十三、最终成果

成功实现：

- 主线驱动移植
- API 适配
- 固件加载
- IIO 注册
- Sysfs 读取
- 用户态数据获取