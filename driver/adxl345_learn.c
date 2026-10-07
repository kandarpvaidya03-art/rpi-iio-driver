// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>

#define ADXL345_REG_DEVID		0x00
#define ADXL345_REG_POWER_CTL		0x2d
#define ADXL345_REG_DATAX0		0x32
#define ADXL345_REG_DATAY0		0x34
#define ADXL345_REG_DATAZ0		0x36

#define ADXL345_DEVID			0xe5
#define ADXL345_POWER_CTL_MEASURE	BIT(3)

struct adxl345_learn_state {
	struct regmap *regmap;
};

#define ADXL345_LEARN_CHAN(_reg, _axis) {			\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.address = (_reg),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
}

static const struct iio_chan_spec adxl345_learn_channels[] = {
	ADXL345_LEARN_CHAN(ADXL345_REG_DATAX0, X),
	ADXL345_LEARN_CHAN(ADXL345_REG_DATAY0, Y),
	ADXL345_LEARN_CHAN(ADXL345_REG_DATAZ0, Z),
};

static int adxl345_learn_read_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int *val, int *val2, long mask)
{
	struct adxl345_learn_state *st = iio_priv(indio_dev);
	__le16 sample;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_bulk_read(st->regmap, chan->address,
				       &sample, sizeof(sample));
		if (ret)
			return ret;
		*val = (s16)le16_to_cpu(sample);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info adxl345_learn_info = {
	.read_raw = adxl345_learn_read_raw,
};

static const struct regmap_config adxl345_learn_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x39,
};

static void adxl345_learn_standby(void *data)
{
	struct adxl345_learn_state *st = data;

	regmap_write(st->regmap, ADXL345_REG_POWER_CTL, 0);
}

static int adxl345_learn_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct adxl345_learn_state *st;
	struct iio_dev *indio_dev;
	unsigned int id;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->regmap = devm_regmap_init_i2c(client, &adxl345_learn_regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "regmap init failed\n");

	ret = regmap_read(st->regmap, ADXL345_REG_DEVID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read device ID\n");
	if (id != ADXL345_DEVID)
		return dev_err_probe(dev, -ENODEV,
				     "unexpected device ID 0x%02x\n", id);

	ret = regmap_write(st->regmap, ADXL345_REG_POWER_CTL,
			   ADXL345_POWER_CTL_MEASURE);
	if (ret)
		return dev_err_probe(dev, ret, "failed to start measurement\n");

	ret = devm_add_action_or_reset(dev, adxl345_learn_standby, st);
	if (ret)
		return ret;

	indio_dev->name = "adxl345_learn";
	indio_dev->info = &adxl345_learn_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adxl345_learn_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxl345_learn_channels);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "IIO registration failed\n");

	dev_info(dev, "ADXL345 found (ID 0x%02x)\n", id);
	return 0;
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
};
module_i2c_driver(adxl345_learn_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kandarp");
MODULE_DESCRIPTION("Learning IIO driver for the ADXL345 accelerometer");
