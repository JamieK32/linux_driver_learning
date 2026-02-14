
---

# 01_gpio_led

> RGB LED Linux kernel driver based on **platform_driver + Device Tree + gpiod + timer + sysfs**
> Target platform: Raspberry Pi 4 (bcm2711)

---

# 1. Project Overview

This project implements a GPIO-based RGB LED kernel driver with the following features:

* Uses the **platform_driver** model
* Device instantiated via **Device Tree Overlay**
* GPIOs acquired using the **gpiod consumer API**
* LED blinking implemented with **kernel timer (timer_list)**
* Runtime configuration through **sysfs interface**

  * Adjustable blinking period
  * RGB color selection (bitmask)

This is the first module in a structured Linux driver learning roadmap, focusing on GPIO fundamentals.

---

# 2. Hardware Connection

## 2.1 Wiring Table

| Color | GPIO (BCM) | Physical Pin | Notes                |
| ----- | ---------- | ------------ | -------------------- |
| R     | 13         | Pin 33       | 220Ω series resistor |
| G     | 19         | Pin 35       | 220Ω series resistor |
| B     | 26         | Pin 37       | 220Ω series resistor |
| GND   | GND        | Any GND      | Common cathode       |

> RGB LED type: **Common Cathode**

---

## 2.2 Hardware Setup

![Hardware Wiring](images/hardware.jpg)

---

# 3. Device Tree Overlay

File: `myled-overlay.dts`

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target-path = "/";
        __overlay__ {
            myled {
                compatible = "mycompany,myled";
                led-r-gpios = <&gpio 13 0>;
                led-g-gpios = <&gpio 19 0>;
                led-b-gpios = <&gpio 26 0>;
            };
        };
    };
};
```

---

## Compile Overlay

```bash
dtc -@ -I dts -O dtb -o myled.dtbo myled-overlay.dts
sudo cp myled.dtbo /boot/overlays/
```

Add the following line to `/boot/config.txt`:

```
dtoverlay=myled
```

Reboot:

```bash
sudo reboot
```

---

# 4. Driver Build & Load

## 4.1 Build

```bash
make
```

## 4.2 Load Module

```bash
sudo insmod gpio_led.ko
```

## 4.3 Check Kernel Log

```bash
dmesg | grep myled
```

### Runtime Log Screenshot

![dmesg output](images/dmesg.png)

Expected output:

```
myled: GPIO 13 19 26 LED blink driver loaded
```

---

# 5. Sysfs Interface

After loading, the driver creates:

```
/sys/devices/platform/myled/
```

Attributes:

```
period_ms
rgb_color
```

Directory screenshot:

![sysfs directory](images/sysfs_ls.png)

---

# 6. Functional Test

## 6.1 Change Blinking Period

```bash
echo 1000 | sudo tee /sys/devices/platform/myled/period_ms
```

Unit: milliseconds
Minimum allowed: 10ms

---

## 6.2 Change Color

Color is controlled via bitmask (0–7):

| Value | Color  |
| ----- | ------ |
| 0     | Off    |
| 1     | Red    |
| 2     | Green  |
| 3     | Yellow |
| 4     | Blue   |
| 5     | Purple |
| 6     | Cyan   |
| 7     | White  |

Example:

```bash
echo 7 | sudo tee /sys/devices/platform/myled/rgb_color
```

### White Blinking Example

![White blinking](images/blink_white.jpg)

---

# 7. Driver Architecture

Overall driver flow:

```
Device Tree
      ↓
platform_device
      ↓
platform_driver
      ↓
probe()
      ↓
devm_gpiod_get()
      ↓
timer_setup()
      ↓
Create sysfs attributes
```

---

# 8. Kernel Concepts Covered

* platform_driver registration
* of_match_table device matching
* devm_gpiod_get resource management
* timer_list kernel timer
* from_timer macro usage
* sysfs device attributes
* device_create_file
* mod_timer and jiffies
* msecs_to_jiffies conversion

---

# 9. Directory Structure

```
01_gpio_led/
├── gpio_led.c
├── myled-overlay.dts
├── Makefile
├── README.md
└── images/
    ├── hardware.jpg
    ├── dmesg.png
    ├── sysfs_ls.png
    └── blink_white.jpg
```

---

# 10. Summary

This project demonstrates a complete implementation of:

* Device Tree + platform driver model
* GPIO resource management
* Kernel timer usage
* sysfs runtime control interface

It serves as a foundational module for embedded Linux driver development.

Future modules will extend to:

* Interrupt handling (gpio_keys)
* PWM subsystem
* IIO subsystem
* SPI driver
* LED class subsystem
* ASoC audio subsystem

---

# Project Positioning

This project is the first stage in a structured Linux driver development roadmap and establishes a solid foundation for more complex kernel subsystems.