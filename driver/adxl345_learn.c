// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>

#define ADXL345_REG_DEVID		0x00
#define ADXL345_REG_BW_RATE		0x2c
#define ADXL345_REG_POWER_CTL		0x2d
#define ADXL345_REG_INT_ENABLE		0x2e
#define ADXL345_REG_INT_MAP		0x2f
#define ADXL345_REG_DATA_FORMAT		0x31
#define ADXL345_REG_DATAX0		0x32
#define ADXL345_REG_DATAY0		0x34
#define ADXL345_REG_DATAZ0		0x36

#define ADXL345_DEVID			0xe5
#define ADXL345_BW_RATE_MASK		GENMASK(3, 0)
#define ADXL345_BW_RATE_100HZ		0x0a
#define ADXL345_POWER_CTL_MEASURE	BIT(3)
#define ADXL345_INT_DATA_READY		BIT(7)
#define ADXL345_DATA_FORMAT_FULL_RES	BIT(3)

/* Full resolution: 3.9 mg/LSB * 9.80665 m/s^2 per g = 0.038246 m/s^2 per LSB */
#define ADXL345_SCALE_MICRO		38246

/* Output data rate in Hz, indexed by the BW_RATE rate code */
static const int adxl345_learn_samp_freq[][2] = {
	{ 0, 97656 },
	{ 0, 195313 },
	{ 0, 390625 },
	{ 0, 781250 },
	{ 1, 562500 },
	{ 3, 125000 },
	{ 6, 250000 },
	{ 12, 500000 },
	{ 25, 0 },
	{ 50, 0 },
	{ 100, 0 },
	{ 200, 0 },
	{ 400, 0 },
	{ 800, 0 },
	{ 1600, 0 },
	{ 3200, 0 },
};

struct adxl345_learn_state {
	struct regmap *regmap;
};

#define ADXL345_LEARN_CHAN(_reg, _axis) {				\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.address = (_reg),						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.info_mask_shared_by_all_available =				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
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
	unsigned int regval;
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
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = ADXL345_SCALE_MICRO;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = regmap_read(st->regmap, ADXL345_REG_BW_RATE, &regval);
		if (ret)
			return ret;
		regval &= ADXL345_BW_RATE_MASK;
		*val = adxl345_learn_samp_freq[regval][0];
		*val2 = adxl345_learn_samp_freq[regval][1];
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int adxl345_learn_write_raw(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   int val, int val2, long mask)
{
	struct adxl345_learn_state *st = iio_priv(indio_dev);
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		for (i = 0; i < ARRAY_SIZE(adxl345_learn_samp_freq); i++) {
			if (adxl345_learn_samp_freq[i][0] == val &&
			    adxl345_learn_samp_freq[i][1] == val2)
				return regmap_update_bits(st->regmap,
						ADXL345_REG_BW_RATE,
						ADXL345_BW_RATE_MASK, i);
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int adxl345_learn_read_avail(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    const int **vals, int *type, int *length,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (const int *)adxl345_learn_samp_freq;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = ARRAY_SIZE(adxl345_learn_samp_freq) * 2;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static const struct iio_info adxl345_learn_info = {
	.read_raw = adxl345_learn_read_raw,
	.write_raw = adxl345_learn_write_raw,
	.read_avail = adxl345_learn_read_avail,
};

static const struct regmap_config adxl345_learn_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x39,
};

static void adxl345_learn_standby(void *data)
{
	struct adxl345_learn_state *st = data;

	regmap_write(st->regmap, ADXL345_REG_INT_ENABLE, 0);
	regmap_write(st->regmap, ADXL345_REG_POWER_CTL, 0);
}

static irqreturn_t adxl345_learn_irq_thread(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct adxl345_learn_state *st = iio_priv(indio_dev);
	u8 buf[6];

	/* Reading the data registers clears DATA_READY and releases INT1 */
	regmap_bulk_read(st->regmap, ADXL345_REG_DATAX0, buf, sizeof(buf));

	return IRQ_HANDLED;
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

	/* Put the device in a known state: full resolution, 100 Hz */
	ret = regmap_write(st->regmap, ADXL345_REG_DATA_FORMAT,
			   ADXL345_DATA_FORMAT_FULL_RES);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set data format\n");

	ret = regmap_write(st->regmap, ADXL345_REG_BW_RATE,
			   ADXL345_BW_RATE_100HZ);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set data rate\n");

	ret = regmap_write(st->regmap, ADXL345_REG_POWER_CTL,
			   ADXL345_POWER_CTL_MEASURE);
	if (ret)
		return dev_err_probe(dev, ret, "failed to start measurement\n");

	ret = devm_add_action_or_reset(dev, adxl345_learn_standby, st);
	if (ret)
		return ret;

	if (client->irq <= 0)
		return dev_err_probe(dev, -EINVAL,
				     "no interrupt in device tree\n");

	ret = regmap_write(st->regmap, ADXL345_REG_INT_MAP, 0);
	if (ret)
		return dev_err_probe(dev, ret, "failed to map interrupts\n");

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					adxl345_learn_irq_thread, IRQF_ONESHOT,
					"adxl345_learn", indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ\n");

	ret = regmap_write(st->regmap, ADXL345_REG_INT_ENABLE,
			   ADXL345_INT_DATA_READY);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable interrupt\n");

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
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Learning IIO driver for the ADXL345 accelerometer");
