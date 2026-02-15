
---

# 02_gpio_keys

> Single GPIO Key Driver based on **platform_driver + GPIO + IRQ + Linux Input Subsystem**
> Target platform: Raspberry Pi 4 (bcm2711)

---

# 1. Project Overview

This project implements a GPIO-based key driver using the standard Linux **input subsystem** to report key events.

Main features:

* Uses the `platform_driver` model
* Device instantiated via Device Tree Overlay
* GPIO acquired using the `gpiod` consumer API
* GPIO converted to IRQ
* Uses `devm_request_threaded_irq`
* Integrated with Linux input subsystem
* Automatically generates `/dev/input/eventX`
* Includes basic software debounce handling

This project represents the second stage in the Linux driver learning roadmap:
transitioning from active device control to an interrupt-driven model.

---

# 2. Hardware Connection

## 2.1 Wiring Description

| Function | GPIO (BCM) | Physical Pin | Notes                      |
| -------- | ---------- | ------------ | -------------------------- |
| KEY1     | 26         | Pin 32       | GPIO26                     |
| GND      | GND        | Any          | Button connected to ground |

Wiring:

```
BCM26 ---- Button ---- GND
```

The GPIO is configured as:

```
GPIO_ACTIVE_LOW
```

Meaning the signal is low when the button is pressed.

---

## 2.2 Hardware Setup

![Hardware Wiring](images/hardware.jpg)

---

# 3. Device Tree Overlay

File: `mykeys-overlay.dts`

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target-path = "/";
        __overlay__ {
            mykeys {
                compatible = "mycompany,mykeys";
                key-gpios = <&gpio 17 1>;
            };
        };
    };
};
```

---

## Compile Overlay

```bash
dtc -@ -I dts -O dtb -o mykeys.dtbo mykeys-overlay.dts
sudo cp mykeys.dtbo /boot/overlays/
```

Edit `/boot/firmware/config.txt`:

```
dtoverlay=mykeys
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
sudo insmod gpio_keys.ko
```

## 4.3 Check Kernel Log

```bash
dmesg | grep mykeys
```

### dmesg Screenshot

![dmesg log](images/dmesg.png)

Expected output:

```
GPIO key driver loaded
```

---

# 5. Input Subsystem Verification

After the driver is successfully loaded, it registers an input device automatically.

## 5.1 Check Registered Input Devices

```bash
cat /proc/bus/input/devices
```

Screenshot:

![input devices](images/input_devices.png)

You should see:

```
N: Name="my-gpio-key"
```

---

## 5.2 Test with evtest

Install:

```bash
sudo apt install evtest
```

Run:

```bash
sudo evtest
```

Select:

```
my-gpio-key
```

When pressing the button, you should see output similar to:

```
Event: type 1 (EV_KEY), code 28 (KEY_ENTER), value 1
Event: type 1 (EV_KEY), code 28 (KEY_ENTER), value 0
```

Screenshot:

![evtest output](images/evtest.png)

---

# 6. Driver Architecture

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
gpiod_to_irq()
      ↓
devm_request_threaded_irq()
      ↓
IRQ Thread Handler
      ↓
input_report_key()
      ↓
input_sync()
      ↓
/dev/input/eventX
```

---

# 7. Key Technical Concepts

* platform_driver registration
* gpiod consumer API
* GPIO to IRQ conversion
* Interrupt trigger configuration (rising / falling edge)
* Threaded IRQ
* Linux input subsystem architecture
* input_report_key
* input_sync
* Event-driven programming model in Linux

---

# 8. Directory Structure

```
02_gpio_keys/
├── gpio_keys.c
├── mykeys-overlay.dts
├── Makefile
├── README.md
└── images/
    ├── hardware.jpg
    ├── dmesg.png
    ├── input_devices.png
    └── evtest.png
```

---

# 9. Project Significance

This project marks the transition from:

```
Direct GPIO Control
        ↓
Interrupt-driven Design
        ↓
Integration with Kernel Subsystems
```

It represents a core stage in embedded Linux driver development.

---

# 10. Stage Summary

This chapter covers:

* Interrupt registration and management
* IRQ trigger mechanisms
* Software debounce handling
* Integration with the Linux input subsystem
* Generation of standard Linux input events

It lays the foundation for future topics such as:

* PWM subsystem
* I2C sensors
* IIO framework
* SPI drivers
* ASoC audio subsystem

---

# Project Positioning

Second stage of the Linux driver learning roadmap —
GPIO interrupt handling and input subsystem integration.

---