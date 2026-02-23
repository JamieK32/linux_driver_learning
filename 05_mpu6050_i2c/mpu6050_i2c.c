// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include "mpu6050.h"

static const struct regmap_config mpu6050_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int mpu6050_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client,
				      &mpu6050_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return mpu6050_core_probe(&client->dev,
				  regmap);
}

static const struct of_device_id mpu6050_of_match[] = {
	{ .compatible = "mycompany,mpu6050-minimal" },
	{ }
};
MODULE_DEVICE_TABLE(of, mpu6050_of_match);

static struct i2c_driver mpu6050_i2c_driver = {
	.driver = {
		.name = "mpu6050",
		.of_match_table = mpu6050_of_match,
	},
	.probe = mpu6050_i2c_probe,
};

module_i2c_driver(mpu6050_i2c_driver);

MODULE_LICENSE("GPL");