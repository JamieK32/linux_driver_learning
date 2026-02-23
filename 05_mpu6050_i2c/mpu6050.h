// SPDX-License-Identifier: GPL-2.0
#ifndef _MPU6050_H_
#define _MPU6050_H_

#include <linux/regmap.h>
#include <linux/mutex.h>

#define MPU6050_REG_SMPLRT_DIV       0x19
#define MPU6050_REG_CONFIG           0x1A
#define MPU6050_REG_GYRO_CONFIG      0x1B
#define MPU6050_REG_ACCEL_CONFIG     0x1C

#define MPU6050_REG_ACCEL_XOUT_H     0x3B
#define MPU6050_REG_GYRO_XOUT_H      0x43

#define MPU6050_REG_PWR_MGMT_1       0x6B
#define MPU6050_REG_WHO_AM_I         0x75

#define MPU6050_CHIP_ID              0x68

/* FS_SEL bits [4:3] */
#define MPU6050_FS_SEL_MASK          0x18
#define MPU6050_FS_SEL_SHIFT         3

/* We configure DLPF_CFG=3 -> base sample rate assumed 1kHz */
#define MPU6050_BASE_RATE_HZ         1000

struct mpu6050_data {
	struct regmap *regmap;
	struct mutex lock;
};

int mpu6050_core_probe(struct device *dev, struct regmap *regmap);

#endif