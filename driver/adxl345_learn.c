// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

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
	struct iio_trigger *trig;
	/* Set when capture starts; the first sample is read but not pushed */
	bool skip_first;
	/* One scan: X, Y, Z as read from the device, then the timestamp */
	struct {
		__le16 chans[3];
		aligned_s64 ts;
	} scan;
};

#define ADXL345_LEARN_CHAN(_reg, _axis, _index) {			\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.address = (_reg),						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.info_mask_shared_by_all_available =				\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.scan_index = (_index),						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 13,						\
		.storagebits = 16,					\
		.endianness = IIO_LE,					\
	},								\
}

static const struct iio_chan_spec adxl345_learn_channels[] = {
	ADXL345_LEARN_CHAN(ADXL345_REG_DATAX0, X, 0),
	ADXL345_LEARN_CHAN(ADXL345_REG_DATAY0, Y, 1),
	ADXL345_LEARN_CHAN(ADXL345_REG_DATAZ0, Z, 2),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

/* All three axes come from one burst read, so they are always captured together */
static const unsigned long adxl345_learn_scan_masks[] = {
	BIT(0) | BIT(1) | BIT(2),
	0
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
		/* A raw read would clear DATA_READY under the buffer's feet */
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = regmap_bulk_read(st->regmap, chan->address,
				       &sample, sizeof(sample));
		iio_device_release_direct(indio_dev);
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

/* Called by the IIO core when the buffer using this trigger starts or stops */
static int adxl345_learn_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct adxl345_learn_state *st = iio_priv(indio_dev);

	if (!state)
		return regmap_write(st->regmap, ADXL345_REG_INT_ENABLE, 0);

	/*
	 * DATA_READY is usually already set when capture starts, so the first
	 * interrupt fires at once and says nothing about when that sample was
	 * taken. The handler reads that sample to release INT1 but does not
	 * push it.
	 */
	st->skip_first = true;

	return regmap_write(st->regmap, ADXL345_REG_INT_ENABLE,
			    ADXL345_INT_DATA_READY);
}

static const struct iio_trigger_ops adxl345_learn_trigger_ops = {
	.set_trigger_state = adxl345_learn_set_trigger_state,
	.validate_device = iio_trigger_validate_own_device,
};

/* Threaded handler for INT1: hand the event to the IIO trigger */
static irqreturn_t adxl345_learn_irq_thread(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct adxl345_learn_state *st = iio_priv(indio_dev);

	iio_trigger_poll_nested(st->trig);

	return IRQ_HANDLED;
}

/* Runs once per trigger: capture one scan and push it to the buffer */
static irqreturn_t adxl345_learn_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adxl345_learn_state *st = iio_priv(indio_dev);
	s64 ts = iio_get_time_ns(indio_dev);
	int ret;

	/* The burst read also clears DATA_READY and releases INT1 */
	ret = regmap_bulk_read(st->regmap, ADXL345_REG_DATAX0,
			       st->scan.chans, sizeof(st->scan.chans));
	if (ret)
		dev_err_ratelimited(indio_dev->dev.parent,
				    "failed to read sample: %d\n", ret);
	else if (st->skip_first)
		st->skip_first = false;
	else
		iio_push_to_buffers_with_timestamp(indio_dev, &st->scan, ts);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

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

	/* Known state: interrupts off and routed to INT1, full resolution, 100 Hz */
	ret = regmap_write(st->regmap, ADXL345_REG_INT_ENABLE, 0);
	if (ret)
		return dev_err_probe(dev, ret, "failed to disable interrupts\n");

	ret = regmap_write(st->regmap, ADXL345_REG_INT_MAP, 0);
	if (ret)
		return dev_err_probe(dev, ret, "failed to map interrupts\n");

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

	indio_dev->name = "adxl345_learn";
	indio_dev->info = &adxl345_learn_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adxl345_learn_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxl345_learn_channels);
	indio_dev->available_scan_masks = adxl345_learn_scan_masks;

	if (client->irq <= 0)
		return dev_err_probe(dev, -EINVAL,
				     "no interrupt in device tree\n");

	st->trig = devm_iio_trigger_alloc(dev, "%s-dev%d", indio_dev->name,
					  iio_device_id(indio_dev));
	if (!st->trig)
		return -ENOMEM;

	st->trig->ops = &adxl345_learn_trigger_ops;
	iio_trigger_set_drvdata(st->trig, indio_dev);

	ret = devm_iio_trigger_register(dev, st->trig);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register trigger\n");

	indio_dev->trig = iio_trigger_get(st->trig);

	ret = devm_request_threaded_irq(dev, client->irq, NULL,
					adxl345_learn_irq_thread, IRQF_ONESHOT,
					"adxl345_learn", indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ\n");

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      adxl345_learn_trigger_handler,
					      NULL);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set up buffer\n");

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
MODULE_AUTHOR("Kandarp Vaidya");
MODULE_DESCRIPTION("Learning IIO driver for the ADXL345 accelerometer");
