// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>

#define ADXL345_REG_DEVID	0x00
#define ADXL345_DEVID		0xe5

static int adxl345_learn_probe(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, ADXL345_REG_DEVID);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read device ID: %d\n", ret);
		return ret;
	}
	if (ret != ADXL345_DEVID) {
		dev_err(&client->dev, "unexpected device ID 0x%02x\n", ret);
		return -ENODEV;
	}

	dev_info(&client->dev, "ADXL345 found (ID 0x%02x)\n", ret);
	return 0;
}

static void adxl345_learn_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "removed\n");
}

static const struct of_device_id adxl345_learn_of_match[] = {
	{ .compatible = "rpi,adxl345-learn" },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl345_learn_of_match);

static struct i2c_driver adxl345_learn_driver = {
	.driver = {
		.name = "adxl345_learn",
		.of_match_table = adxl345_learn_of_match,
	},
	.probe = adxl345_learn_probe,
	.remove = adxl345_learn_remove,
};
module_i2c_driver(adxl345_learn_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Learning driver for the ADXL345 accelerometer");
