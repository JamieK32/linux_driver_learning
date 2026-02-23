// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/kernel.h>
#include "mpu6050.h"

struct mpu6050_scale {
	int val;
	int val2; /* micro */
};

/*
 * accel scale in m/s^2 per LSB (approx):
 * 2g:  9.80665 / 16384 = 0.00059855
 * 4g:  9.80665 / 8192  = 0.00119710
 * 8g:  9.80665 / 4096  = 0.00239420
 * 16g: 9.80665 / 2048  = 0.00478840
 *
 * Use rounded micro.
 */
static const struct mpu6050_scale accel_scales[] = {
	{ 0, 599 },   /* ±2g  */
	{ 0, 1197 },  /* ±4g  */
	{ 0, 2394 },  /* ±8g  */
	{ 0, 4788 },  /* ±16g */
};

/*
 * gyro scale in rad/s per LSB (approx):
 * 250 dps:  (pi/180)/131   = 0.000133158
 * 500 dps:  (pi/180)/65.5  = 0.000266316
 * 1000 dps: (pi/180)/32.8  = 0.000532632
 * 2000 dps: (pi/180)/16.4  = 0.001065264
 */
static const struct mpu6050_scale gyro_scales[] = {
	{ 0, 133 },   /* ±250 dps  */
	{ 0, 266 },   /* ±500 dps  */
	{ 0, 533 },   /* ±1000 dps */
	{ 0, 1065 },  /* ±2000 dps */
};

/* Keep it simple: only divisors of 1000Hz (base rate) */
static const int sampling_freqs[] = { 10, 20, 25, 50, 100, 200, 250, 500, 1000 };

static int mpu6050_read_u16be(struct mpu6050_data *data, int reg, int *out)
{
	unsigned int hi, lo;
	int ret;

	ret = regmap_read(data->regmap, reg, &hi);
	if (ret)
		return ret;

	ret = regmap_read(data->regmap, reg + 1, &lo);
	if (ret)
		return ret;

	*out = (s16)((hi << 8) | lo);
	return 0;
}

static int mpu6050_get_scale(struct mpu6050_data *data, int chan_type,
			     int *val, int *val2)
{
	unsigned int regval;
	unsigned int idx;
	int ret, reg;

	switch (chan_type) {
	case IIO_ACCEL:
		reg = MPU6050_REG_ACCEL_CONFIG;
		break;
	case IIO_ANGL_VEL:
		reg = MPU6050_REG_GYRO_CONFIG;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(data->regmap, reg, &regval);
	if (ret)
		return ret;

	idx = (regval & MPU6050_FS_SEL_MASK) >> MPU6050_FS_SEL_SHIFT;
	if (idx > 3)
		return -EINVAL;

	if (chan_type == IIO_ACCEL) {
		*val = accel_scales[idx].val;
		*val2 = accel_scales[idx].val2;
	} else {
		*val = gyro_scales[idx].val;
		*val2 = gyro_scales[idx].val2;
	}

	return 0;
}

static int mpu6050_set_scale(struct mpu6050_data *data, int chan_type,
			     int val, int val2)
{
	int i, reg;
	unsigned int bits;

	if (val != 0)
		return -EINVAL;

	switch (chan_type) {
	case IIO_ACCEL:
		reg = MPU6050_REG_ACCEL_CONFIG;
		for (i = 0; i < ARRAY_SIZE(accel_scales); i++) {
			if (accel_scales[i].val == val &&
			    accel_scales[i].val2 == val2)
				break;
		}
		if (i == ARRAY_SIZE(accel_scales))
			return -EINVAL;
		break;

	case IIO_ANGL_VEL:
		reg = MPU6050_REG_GYRO_CONFIG;
		for (i = 0; i < ARRAY_SIZE(gyro_scales); i++) {
			if (gyro_scales[i].val == val &&
			    gyro_scales[i].val2 == val2)
				break;
		}
		if (i == ARRAY_SIZE(gyro_scales))
			return -EINVAL;
		break;

	default:
		return -EINVAL;
	}

	bits = (unsigned int)i << MPU6050_FS_SEL_SHIFT;
	return regmap_update_bits(data->regmap, reg, MPU6050_FS_SEL_MASK, bits);
}

static int mpu6050_get_samp_freq(struct mpu6050_data *data, int *hz)
{
	unsigned int div;
	int ret;

	ret = regmap_read(data->regmap, MPU6050_REG_SMPLRT_DIV, &div);
	if (ret)
		return ret;

	/* sample_rate = base/(1+div) */
	*hz = MPU6050_BASE_RATE_HZ / (1 + (div & 0xFF));
	return 0;
}

static int mpu6050_set_samp_freq(struct mpu6050_data *data, int hz)
{
	int div;

	if (hz <= 0 || hz > MPU6050_BASE_RATE_HZ)
		return -EINVAL;

	/* Require exact divisor for simplicity */
	if (MPU6050_BASE_RATE_HZ % hz)
		return -EINVAL;

	div = (MPU6050_BASE_RATE_HZ / hz) - 1;
	if (div < 0 || div > 255)
		return -EINVAL;

	return regmap_write(data->regmap, MPU6050_REG_SMPLRT_DIV, div);
}

static int mpu6050_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mpu6050_data *data = iio_priv(indio_dev);
	int reg, ret, tmp;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		mutex_lock(&data->lock);

		if (chan->type == IIO_ACCEL)
			reg = MPU6050_REG_ACCEL_XOUT_H;
		else if (chan->type == IIO_ANGL_VEL)
			reg = MPU6050_REG_GYRO_XOUT_H;
		else {
			mutex_unlock(&data->lock);
			iio_device_release_direct_mode(indio_dev);
			return -EINVAL;
		}

		switch (chan->channel2) {
		case IIO_MOD_X: reg += 0; break;
		case IIO_MOD_Y: reg += 2; break;
		case IIO_MOD_Z: reg += 4; break;
		default:
			mutex_unlock(&data->lock);
			iio_device_release_direct_mode(indio_dev);
			return -EINVAL;
		}

		ret = mpu6050_read_u16be(data, reg, &tmp);
		mutex_unlock(&data->lock);
		iio_device_release_direct_mode(indio_dev);
		if (ret)
			return ret;

		*val = tmp;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&data->lock);
		ret = mpu6050_get_scale(data, chan->type, val, val2);
		mutex_unlock(&data->lock);
		if (ret)
			return ret;
		return IIO_VAL_INT_PLUS_MICRO;

	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->lock);
		ret = mpu6050_get_samp_freq(data, &tmp);
		mutex_unlock(&data->lock);
		if (ret)
			return ret;

		*val = tmp;
		*val2 = 0;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int mpu6050_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct mpu6050_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		mutex_lock(&data->lock);
		ret = mpu6050_set_scale(data, chan->type, val, val2);
		mutex_unlock(&data->lock);

		iio_device_release_direct_mode(indio_dev);
		return ret;

	case IIO_CHAN_INFO_SAMP_FREQ:
		/* sysfs 里一般只写整数 Hz，val2 应为 0 */
		if (val2 != 0)
			return -EINVAL;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		mutex_lock(&data->lock);
		ret = mpu6050_set_samp_freq(data, val);
		mutex_unlock(&data->lock);

		iio_device_release_direct_mode(indio_dev);
		return ret;

	default:
		return -EINVAL;
	}
}

static int mpu6050_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type,
			      int *length, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_INT_PLUS_MICRO;

		if (chan->type == IIO_ACCEL) {
			*vals = (const int *)accel_scales;
			*length = ARRAY_SIZE(accel_scales) * 2;
			return IIO_AVAIL_LIST;
		}

		if (chan->type == IIO_ANGL_VEL) {
			*vals = (const int *)gyro_scales;
			*length = ARRAY_SIZE(gyro_scales) * 2;
			return IIO_AVAIL_LIST;
		}

		return -EINVAL;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT;
		*vals = sampling_freqs;
		*length = ARRAY_SIZE(sampling_freqs);
		return IIO_AVAIL_LIST;

	default:
		return -EINVAL;
	}
}

static const struct iio_info mpu6050_info = {
	.read_raw   = mpu6050_read_raw,
	.write_raw  = mpu6050_write_raw,
	.read_avail = mpu6050_read_avail,
};

#define MPU6050_ACCEL_CHANNEL(_axis) {					\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
}

#define MPU6050_GYRO_CHANNEL(_axis) {					\
	.type = IIO_ANGL_VEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
}

static const struct iio_chan_spec mpu6050_channels[] = {
	MPU6050_ACCEL_CHANNEL(X),
	MPU6050_ACCEL_CHANNEL(Y),
	MPU6050_ACCEL_CHANNEL(Z),

	MPU6050_GYRO_CHANNEL(X),
	MPU6050_GYRO_CHANNEL(Y),
	MPU6050_GYRO_CHANNEL(Z),
};

int mpu6050_core_probe(struct device *dev, struct regmap *regmap)
{
	struct iio_dev *indio_dev;
	struct mpu6050_data *data;
	unsigned int chip_id;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->regmap = regmap;
	mutex_init(&data->lock);

	/* WHO_AM_I */
	ret = regmap_read(regmap, MPU6050_REG_WHO_AM_I, &chip_id);
	if (ret)
		return ret;

	if (chip_id != MPU6050_CHIP_ID)
		return -ENODEV;

	/*
	 * Wake up + select clock source (PLL with X axis gyroscope).
	 * 0x01 is common stable choice.
	 */
	ret = regmap_write(regmap, MPU6050_REG_PWR_MGMT_1, 0x01);
	if (ret)
		return ret;

	/* CONFIG: DLPF_CFG=3 (typical) -> base rate assumed 1kHz */
	ret = regmap_write(regmap, MPU6050_REG_CONFIG, 0x03);
	if (ret)
		return ret;

	/* Default sample rate = 100Hz => div = 1000/100 - 1 = 9 */
	ret = regmap_write(regmap, MPU6050_REG_SMPLRT_DIV, 9);
	if (ret)
		return ret;

	/* Default accel range: ±2g => FS_SEL=0 */
	ret = regmap_update_bits(regmap, MPU6050_REG_ACCEL_CONFIG,
				 MPU6050_FS_SEL_MASK, 0 << MPU6050_FS_SEL_SHIFT);
	if (ret)
		return ret;

	/* Default gyro range: ±250 dps => FS_SEL=0 */
	ret = regmap_update_bits(regmap, MPU6050_REG_GYRO_CONFIG,
				 MPU6050_FS_SEL_MASK, 0 << MPU6050_FS_SEL_SHIFT);
	if (ret)
		return ret;

	indio_dev->name = "mpu6050";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mpu6050_info;
	indio_dev->channels = mpu6050_channels;
	indio_dev->num_channels = ARRAY_SIZE(mpu6050_channels);

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_GPL(mpu6050_core_probe);

MODULE_LICENSE("GPL");