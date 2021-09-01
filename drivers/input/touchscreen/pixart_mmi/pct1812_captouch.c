//
// Description: I2C Adapter for PixArt Imaging PCT1812FF Capacitance Touch Controller Driver on Linux Kernel
//
// Copyright (c) 2021 Lenovo, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
#define MYNAME "pct1812_mmi"
#define pr_fmt(fmt) "pct1812_mmi: %s: " fmt, __func__

#define CALM_WDOG
#define SHOW_EVERYTHING
//#define DRY_RUN_UPDATE
#define SHOW_I2C_DATA
//#define STATIC_PLATFORM_DATA

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <linux/kfifo.h>

// Comment the following section out if it's too much :-)
#if defined(SHOW_EVERYTHING)
#ifdef pr_debug
#undef pr_debug
#define pr_debug pr_err
#endif
#ifdef dev_dbg
#undef dev_dbg
#define dev_dbg dev_err
#endif
#endif

#define AREAD (1)
#define AWRITE (0)

#define INVALID_BYTE2 0xebeb
#define INVALID_BYTE 0xeb

#define ADDRESS_FW 0x00
#define ADDRESS_PARAMS 0xe0

#define SUPPORTED_FW_FILE_SIZE 0x0000F000
#define FW_SECTION_SIZE 0x0000E000

#define PCT1812_SECTOR_SIZE (0x1000) // 4K
#define CHUNK_SZ 256

#define FW_ADDR_BASE 0x00
#define PARAM_ADDR_BASE 0xe0

#define POWERUP_CODE 0x02
#define RESET_CODE 0xaa
#define RESUME_CODE 0xbb
#define ENGINEERING_CODE 0xcc
#define SUSPEND_CODE 0x99

#define FRAME_NUM_REG 0x00

#define NUM_OBJ_REG 0x01
#define OBJ_X0L_REG 0x04
#define OBJ_X0H_REG 0x05
#define OBJ_Y0L_REG 0x06
#define OBJ_Y0H_REG 0x07
#define OBJ_0_BASE OBJ_X0L_REG

#define OBJ_ADDR(o) (OBJ_0_BASE + (o) * 4)

#define GEST_TYPE_REG 0x60
#define GEST_X0L_REG 0x62
#define GEST_X0H_REG 0x63
#define GEST_Y0L_REG 0x64
#define GEST_Y0H_REG 0x65
#define GEST_0_BASE GEST_X0L_REG

#define GEST_ADDR(g) (GEST_0_BASE + (g) * 4)

#define BOOT_STATUS_REG 0x70
#define STATUS_REG 0x71

#define USER_BANK_REG 0x73
#define USER_ADDR_REG 0x74
#define USER_DATA_REG 0x75

#define SW_RESET_REG 0x7a
#define DEEP_SLEEP_REG 0x7c
#define MODE_REG 0x7f

#define BANK(a) (a)

// USER BANK 0
#define PROD_ID_REG 0x00
#define FW_REV_REG 0x01
#define FW_VMIN_L_REG 0x02
#define FW_MAJOR_REG 0x03
#define MODEL_REG 0x04
#define FW_PATCH_REG 0x06
#define INTR_MASK_REG 0x07
#define TX_REG 0x5a
#define RX_REG 0x59
#define VER_LOW_REG 0x7e
#define VER_HIGH_REG 0x7f

// USER BANK 1
#define FLASH_PUP_REG 0x0d
#define KEY1_REG 0x2c
#define KEY2_REG 0x2d

// USER BANK 2
#define XRES_L_REG 0x00
#define XRES_H_REG 0x01
#define YRES_L_REG 0x02
#define YRES_H_REG 0x03
#define MAX_POINTS_REG 0x05

// USER BANK 3
#define FUNCT_CTRL_REG 0x02

// USER BANK 4
#define FLASH_STATUS_REG 0x1c
#define FLASH_ADDR0_REG 0x24
#define FLASH_ADDR1_REG 0x25
#define FLASH_ADDR2_REG 0x26

#define STAT_BIT_ERR (1 << 0)
#define STAT_BIT_TOUCH (1 << 1)
#define STAT_BIT_GESTURE (1 << 3)
#define STAT_BIT_WDOG (1 << 7)
#define STAT_BIT_EABS (STAT_BIT_WDOG | STAT_BIT_TOUCH)
#define STAT_BIT_ALL 0xff

#define SLEEP_BIT_DISABLE (1 << 0)
#define SLEEP_BIT_STAT (1 << 4)

#define BOOT_COMPLETE 1
#define MODE_COMPLETE (BOOT_COMPLETE | (1 <<7))
#define TOUCH_RESET_DELAY 10
#define CMD_WAIT_DELAY 10
#define WDOG_INTERVAL 1000
#define SELFTEST_SHORT_INTERVAL 3000
#define SELFTEST_LONG_INTERVAL 5000
#define SELFTEST_EXTRA_LONG_INTERVAL 10000
#define PON_DELAY 200

#define CNT_I2C_RETRY 3
#define CNT_WAIT_RETRY 50

#define SNR_LOOP_COUNT 50

#define I2C_WRITE_BUFFER_SIZE 4
#define I2C_READ_BUFFER_SIZE 4

#define PINCTRL_STATE_ACTIVE "touchpad_active"
#define PINCTRL_STATE_SUSPEND "touchpad_suspend"

enum pwr_modes {
	PWR_MODE_OFF,
	PWR_MODE_RUN,
	PWR_MODE_LPM,
	PWR_MODE_DEEP_SLEEP,
	PWR_MODE_SHUTDOWN,
	PWR_MODE_MAX
};

enum cmds {
	CMD_INIT,
	CMD_RESET,
	CMD_SUSPEND,
	CMD_RESUME,
	CMD_RECOVER,
	CMD_WDOG,
	CMD_MODE_RESET,
	CMD_CLEANUP
};

enum selftest_ids {
	PCT1812_SELFTEST_NONE,
	PCT1812_SELFTEST_FULL = 1,
	PCT1812_SELFTEST_SNR,
	PCT1812_SELFTEST_DRAW_LINE,
	PCT1812_SELFTEST_IN_PROGRESS,
	PCT1812_SELFTEST_MAX
};

enum gestures {
	GEST_TAP = 2,
	GEST_DBL_TAP,
	GEST_VERT_SCROLL = 7,
	GEST_HORIZ_SCROLL,
	GEST_UP_SWIPE = 17,
	GEST_DWN_SWIPE,
	GEST_LFT_SWIPE,
	GEST_RGHT_SWIPE
};

struct pct1812_platform {
	int max_x;
	int max_y;

	unsigned rst_gpio;
	unsigned irq_gpio;

	struct i2c_client *client;

	const char *vdd;
	const char *vio;
};

#if defined(STATIC_PLATFORM_DATA)
#define VDD_PS_NAME "pm8350c_l3"
#define VIO_PS_NAME "pm8350_s10"

static struct pct1812_platform pct1812_pd = {
	0, 0,
	0, 84,
	NULL, VDD_PS_NAME, VIO_PS_NAME
};
#endif

struct pct1812_data {
	struct device *dev;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct pct1812_platform *plat_data;

	struct regulator *reg_vdd;
	struct regulator *reg_vio;

	int tx_count;
	int rx_count;

	int selftest;
	int frame_count;
	int frame_sample;
	int test_type;
	ktime_t test_start_time;
	unsigned int frame_size;
	unsigned int test_hdr_size;
	unsigned int test_data_size;
	unsigned char **test_data;
	unsigned char test_intr_mask;

	struct mutex i2c_mutex;
	struct mutex eventlock;
	struct mutex cmdlock;
	struct delayed_work worker;

	bool irq_enabled;
	struct kfifo cmd_pipe;
	atomic_t touch_stopped;

	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	struct pinctrl_state *pinctrl_state_suspend;

	unsigned char drop_mask;
	unsigned short version;
	unsigned char func_ctrl;
	unsigned char model;
	unsigned char major;
	unsigned short minor;
	unsigned char revision;
	unsigned char patch;
	unsigned char product_id;
	unsigned short x_res;
	unsigned short y_res;
	unsigned char max_points;
};

static ssize_t selftest_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t selftest_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t doreflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t forcereflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t ic_ver_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t drv_irq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t drv_irq_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t irq_status_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t info_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t mask_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);
static ssize_t mask_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t selftest_bin_show(struct file *filp, struct kobject *kobp,
		struct bin_attribute *bin_attr, char *buf, loff_t pos, size_t count);
		
static DEVICE_ATTR_WO(doreflash);
static DEVICE_ATTR_WO(forcereflash);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_RO(ic_ver);
static DEVICE_ATTR_RO(info);
static DEVICE_ATTR_RO(vendor);
static DEVICE_ATTR_RO(irq_status);
static DEVICE_ATTR_RW(selftest);
static DEVICE_ATTR_RW(drv_irq);
static DEVICE_ATTR_RW(mask);

static struct attribute *pct1812_attrs[] = {
	&dev_attr_doreflash.attr,
	&dev_attr_forcereflash.attr,
	&dev_attr_reset.attr,
	&dev_attr_ic_ver.attr,
	&dev_attr_vendor.attr,
	&dev_attr_drv_irq.attr,
	&dev_attr_irq_status.attr,
	&dev_attr_selftest.attr,
	&dev_attr_info.attr,
	&dev_attr_mask.attr,
	NULL,
};

static struct attribute_group pct1812_attrs_group = {
	.attrs = pct1812_attrs,
};

static struct bin_attribute selftest_bin_attr = {
	.attr = {.name = "selftest_bin", .mode = 0440},
	.read = selftest_bin_show,
};

static int inline pct1812_selftest_get(struct pct1812_data *ts)
{
	int cur = PCT1812_SELFTEST_NONE;

	mutex_lock(&ts->cmdlock);
	cur = ts->selftest;
	mutex_unlock(&ts->cmdlock);

	return cur;
}

static void inline pct1812_selftest_set(struct pct1812_data *ts, enum selftest_ids mode)
{
	mutex_lock(&ts->cmdlock);
	if (ts->selftest != mode)
		ts->selftest = mode;
	mutex_unlock(&ts->cmdlock);
}

static void inline pct1812_drop_mask_set(struct pct1812_data *ts, unsigned char mask)
{
	mutex_lock(&ts->eventlock);
	ts->drop_mask = mask;
	mutex_unlock(&ts->eventlock);
}

static void inline pct1812_fifo_cmd_add(struct pct1812_data *ts, enum cmds command, unsigned int delay)
{
	kfifo_put(&ts->cmd_pipe, command);
	schedule_delayed_work(&ts->worker, msecs_to_jiffies(delay));
}

static void inline pct1812_delay_ms(unsigned int ms)
{
	if (ms < 20)
		usleep_range(ms * 1000, ms * 1000);
	else
		msleep(ms);
}

static int pct1812_i2c_read(struct pct1812_data *ts, unsigned char reg, unsigned char *data, int len)
{
	unsigned char retry, buf[I2C_READ_BUFFER_SIZE];
	int ret;
	struct i2c_msg msg[2];
#if 0
	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		dev_err(&ts->client->dev, "%s: POWER_STATUS : OFF\n", __func__);
		return -EIO;
	}
#endif
	buf[0] = reg;

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf;

	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	mutex_lock(&ts->i2c_mutex);
	for (retry = 0; retry < CNT_I2C_RETRY; retry++) {
		ret = i2c_transfer(ts->client->adapter, msg, 2);
		if (ret == 2) {
			ret = 0; /* indicate a success */
			break;
		}
#if 0
		if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				dev_err(&ts->client->dev, "%s: POWER_STATUS : OFF, retry:%d\n", __func__, retry);
				mutex_unlock(&ts->i2c_mutex);
				return -EIO;
		}
#endif
		pct1812_delay_ms(1);
		if (retry > 1) {
			dev_err(&ts->client->dev, "%s: I2C retry %d, ret:%d\n", __func__, retry + 1, ret);
			//ts->comm_err_count++;
		}
	}
	mutex_unlock(&ts->i2c_mutex);

	if (retry == CNT_I2C_RETRY) {
		dev_err(&ts->client->dev, "%s: I2C read over retry limit\n", __func__);
		ret = -EIO;
	}
#ifdef SHOW_I2C_DATA
	if (1) {
		int i;
		pr_info("i2c_cmd: R: %02X | ", reg);
		for (i = 0; i < len; i++)
			pr_cont("%02X ", data[i]);
		pr_cont("\n");
	}
#endif
	return ret;
}

static int pct1812_i2c_write(struct pct1812_data *ts, unsigned char reg, unsigned char *data, int len)
{
	unsigned char retry, buf[I2C_WRITE_BUFFER_SIZE + 1];
	int ret;
	struct i2c_msg msg;

	if (len > I2C_WRITE_BUFFER_SIZE) {
		dev_err(&ts->client->dev, "%s: len is larger than buffer size\n", __func__);
		return -EINVAL;
	}
#if 0
	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		dev_err(&ts->client->dev, "%s: POWER_STATUS : OFF\n", __func__);
		return -EIO;
	}
#endif
	buf[0] = reg;
	memcpy(buf + 1, data, len);

	msg.addr = ts->client->addr;
	msg.flags = 0;
	msg.len = len + 1;
	msg.buf = buf;

	mutex_lock(&ts->i2c_mutex);
	for (retry = 0; retry < CNT_I2C_RETRY; retry++) {
		ret = i2c_transfer(ts->client->adapter, &msg, 1);
		if (ret == 1) {
			ret = 0; /* indicate a success */
			break;
		}
#if 0
		if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
			dev_err(&ts->client->dev, "%s: POWER_STATUS : OFF, retry:%d\n", __func__, retry);
			mutex_unlock(&ts->i2c_mutex);
			return -EIO;
		}
#endif
		pct1812_delay_ms(1);
		if (retry > 1) {
			dev_err(&ts->client->dev, "%s: I2C retry %d, ret:%d\n", __func__, retry + 1, ret);
			//ts->comm_err_count++;
		}
	}
	mutex_unlock(&ts->i2c_mutex);

	if (retry == CNT_I2C_RETRY) {
		dev_err(&ts->client->dev, "%s: I2C write over retry limit\n", __func__);
		ret = -EIO;
	}
#ifdef SHOW_I2C_DATA
	if (1) {
		int i;
		pr_info("i2c_cmd: W: %02X | ", reg);
		for (i = 0; i < len; i++)
			pr_cont("%02X ", data[i]);
		pr_cont("\n");
	}
#endif
	return ret;
}

static void inline pct1812_set_irq(struct pct1812_data *ts, bool on)
{
	if (on) {
		if (!ts->irq_enabled) {
			ts->irq_enabled = true;
			enable_irq(ts->plat_data->client->irq);
			pr_debug("IRQ enabled\n");
		}
	} else {
		if (ts->irq_enabled) {
			ts->irq_enabled = false;
			disable_irq(ts->plat_data->client->irq);
			pr_debug("IRQ disabled\n");
		}
	}
}

#ifndef STATIC_PLATFORM_DATA
static int pct1812_parse_dt(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pct1812_platform *pdata = dev->platform_data;
	struct device_node *np = dev->of_node;
	unsigned int coords[2];
	int ret = 0;

	if (of_property_read_string(np, "pct1812,regulator_vdd", &pdata->vdd)) {
		dev_err(dev, "%s: Failed to get VDD name property\n", __func__);
		return -EINVAL;
	}

	if (of_property_read_string(np, "pct1812,regulator_vio", &pdata->vio)) {
		dev_warn(dev, "%s: Failed to get VIO name property\n", __func__);
		//return -EINVAL;
	}

	pdata->irq_gpio = of_get_named_gpio(np, "pct1812,irq-gpio", 0);
	if (!gpio_is_valid(pdata->irq_gpio)) {
		dev_err(&client->dev, "%s: Failed to get irq gpio\n", __func__);
		return -EINVAL;
	}

	pdata->rst_gpio = of_get_named_gpio(np, "pct1812,reset-gpio", 0);
	if (!gpio_is_valid(pdata->rst_gpio))
		pdata->rst_gpio = -EINVAL;

	if (!of_property_read_u32_array(np, "pct1812,max_coords", coords, 2)) {
		pdata->max_x = coords[0] - 1;
		pdata->max_y = coords[1] - 1;
		dev_info(dev, "%s: max_coords:(%d,%d)\n", __func__, pdata->max_x, pdata->max_y);
	}

	return ret;
}
#endif

static int pct1812_platform_init(struct pct1812_platform *pdata)
{
	struct device *dev = &pdata->client->dev;
	int ret;

	if (gpio_is_valid(pdata->irq_gpio)) {
		ret = gpio_request_one(pdata->irq_gpio, GPIOF_DIR_IN, "pct1812,irq");
		if (ret) {
			dev_err(dev, "%s: Unable to request gpio [%d]\n", __func__, pdata->irq_gpio);
			return -EINVAL;
		}
		dev_info(dev, "%s: using irq gpio %d\n", __func__, pdata->irq_gpio);
	}

	if (gpio_is_valid(pdata->rst_gpio)) {
		ret = gpio_request_one(pdata->rst_gpio, GPIOF_DIR_OUT, "pct1812,reset");
		if (ret) {
			dev_err(dev, "%s: Unable to request gpio [%d]\n", __func__, pdata->rst_gpio);
			return -EINVAL;
		}
		dev_info(dev, "%s: using reset gpio %d\n", __func__, pdata->rst_gpio);
	}

	pdata->client->irq = gpio_to_irq(pdata->irq_gpio);

	return 0;
}

static int inline comp2_16b(unsigned char *data)
{
	int result;
	/* 16bits 2's compliment data */
	if (data[1] & 0x80)
		result = (data[0] | (data[1] << 8) | 0xFFFF0000);
	else
		result = (data[0] | (data[1] << 8));

	return result;
}

#define X0L 0
#define X0H 1
#define Y0L 2
#define Y0H 3

#define X_GET(d) (&d[X0L])
#define Y_GET(d) (&d[Y0L])

#define UINT16_X(d) ((d[1] << 8) | d[0] | 0x00000000)
#define UINT16_Y(d) ((d[3] << 8) | d[2] | 0x00000000)

static int pct1812_read_xy_coords(struct pct1812_data *ts,
			unsigned char start_addr, unsigned char *tdata)
{
	int ret;

	ret = pct1812_i2c_read(ts, start_addr, &tdata[X0L], sizeof(tdata[X0L]));
	ret = !ret && pct1812_i2c_read(ts, start_addr + 1, &tdata[X0H], sizeof(tdata[X0H]));
	ret = !ret && pct1812_i2c_read(ts, start_addr + 2, &tdata[Y0L], sizeof(tdata[Y0L]));
	ret = !ret && pct1812_i2c_read(ts, start_addr + 3, &tdata[Y0H], sizeof(tdata[Y0H]));

	return ret ? -EIO : 0;
}

static int inline pct1812_selftest_push(struct pct1812_data *ts,
		unsigned char *coords, unsigned int length)
{
	int ret = -ERANGE;

	if ((ts->frame_sample + 1) <= ts->frame_count) {
		memcpy(ts->test_data[ts->frame_sample], coords, length);
		ts->frame_sample++;
		ts->test_data_size += length;
		pr_debug("push test sample to slot %d; data size %u\n", ts->frame_sample, ts->test_data_size);
		ret = 0;
	} else
		dev_warn(ts->dev, "%s: Buffer is full\n", __func__);

	return ret;
}

static int pct1812_process_touch_event(struct pct1812_data *ts)
{
	unsigned char start_addr, nt, coords[4] = {0};
	int i, mode, ret;

	ret = pct1812_i2c_read(ts, NUM_OBJ_REG, &nt, sizeof(nt));
	if (ret)
		return -EIO;
	pr_debug("num objects %d\n", (int)nt);
	for (i = 0; i < nt; i++) {
		start_addr = OBJ_ADDR(i);
		ret = pct1812_read_xy_coords(ts, start_addr, coords);
		if (ret) {
			dev_err(ts->dev, "%s: error reading obj %d\n", __func__, i);
			return -EIO;
		} else if (i == 0) { // draw line self-test supports one finger only
			mode = pct1812_selftest_get(ts);
			if (mode == PCT1812_SELFTEST_DRAW_LINE)
				pct1812_selftest_push(ts, coords, sizeof(coords));

			pr_debug("[@%02x]: x=%d, y=%d\n", start_addr, UINT16_X(coords), UINT16_Y(coords));
		}
	}

	return 0;
}

static int pct1812_process_gesture_event(struct pct1812_data *ts)
{
	unsigned char gest, coords[4] = {0};
	int ret;

	ret = pct1812_i2c_read(ts, GEST_TYPE_REG, &gest, sizeof(gest));
	if (ret)
		return -EIO;

	pr_debug("gesture 0x%02x\n", gest);
	switch(gest & 0x1f) {
		case GEST_HORIZ_SCROLL:
		case GEST_VERT_SCROLL:
						break;
		case GEST_TAP:
		case GEST_DBL_TAP:
		case GEST_UP_SWIPE:
		case GEST_DWN_SWIPE:
		case GEST_LFT_SWIPE:
		case GEST_RGHT_SWIPE:
						return 0;
		default: return -EINVAL;
	}

	ret = pct1812_read_xy_coords(ts, GEST_ADDR(0), coords);
	if (ret) {
		dev_err(ts->dev, "%s: error reading gesture\n", __func__);
	} else
		pr_debug("[@%02x]: x=%d, y=%d\n", GEST_ADDR(0),
					comp2_16b(X_GET(coords)), comp2_16b(Y_GET(coords)));

	return ret;
}

static irqreturn_t pct1812_irq_handler(int irq, void *ptr)
{
	struct pct1812_data *ts = (struct pct1812_data *)ptr;
	unsigned char dropped, status = 0;
	bool ack = true;
	int ret;

	mutex_lock(&ts->eventlock);
	ret = pct1812_i2c_read(ts, STATUS_REG, &status, sizeof(status));
	dropped = status;
	dropped &= ~ts->drop_mask;
	if (status != dropped)
		pr_debug("drop out %02x -> %02x\n", status, dropped);
	if (dropped & STAT_BIT_GESTURE) {
		pct1812_process_gesture_event(ts);
	} else if (dropped & STAT_BIT_TOUCH) {
		pct1812_process_touch_event(ts);
	} else if ((status & STAT_BIT_ERR) || (status & STAT_BIT_WDOG)) {
		ack = false;
		pct1812_fifo_cmd_add(ts, CMD_RECOVER, TOUCH_RESET_DELAY);
	}
	if (ack) {
		status = 0; // ACK interrupt
		ret = pct1812_i2c_write(ts, STATUS_REG, &status, sizeof(status));
	}
	mutex_unlock(&ts->eventlock);

	return IRQ_HANDLED;
}

static int pct1812_user_access(struct pct1812_data *ts,
	unsigned char bank, unsigned char addr,
	unsigned char *value, unsigned int size, bool R)
{
	unsigned char val = INVALID_BYTE;
	int ret;

	ret = pct1812_i2c_write(ts, USER_BANK_REG, &bank, sizeof(bank));
	if (ret)
		return -EIO;

	ret = pct1812_i2c_write(ts, USER_ADDR_REG, &addr, sizeof(addr));
	if (ret)
		return -EIO;

	if (R) { // read access
		if (value)
			*value = 0;
		ret = pct1812_i2c_read(ts, USER_DATA_REG, &val, sizeof(val));
		if (!ret && value)
			*value = val;
	} else {
		ret = pct1812_i2c_write(ts, USER_DATA_REG, value, size);
	}

	pr_debug("%c: b%d:0x%02x val 0x%02x\n",
			R ? 'R' : 'W', bank, addr, R ? val : *value);

	return ret;
}

#define QUERY_PARAM_IF(p, b, r, v) { \
	p = INVALID_BYTE; \
	ret = pct1812_user_access(ts, b, r, &(v), sizeof(v), AREAD); \
	if (!ret) { \
		p = v; \
	} else { \
		dev_err(ts->dev, "%s: Error reading b(%d):0x%02x\n", __func__, b, r); \
		goto failure; \
	} \
}

#define QUERY_PARAM(p, b, r, v) { \
	p = INVALID_BYTE; \
	ret = pct1812_user_access(ts, b, r, &(v), sizeof(v), AREAD); \
	if (!ret) { \
		p = v; \
	} else { \
		dev_err(ts->dev, "%s: Error reading b(%d):0x%02x\n", __func__, b, r); \
	} \
}

#define SET_PARAM(b, r, v) { \
	ret = pct1812_user_access(ts, b, r, &(v), sizeof(v), AWRITE); \
	if (ret) { \
		dev_err(ts->dev, "%s: Error writing b(%d):0x%02x\n", __func__, b, r); \
		goto failure; \
	} \
}

#define GET_REG_IF(r, v) { \
	v = INVALID_BYTE; \
	ret = pct1812_i2c_read(r, &(v), sizeof(v)); \
	if (ret) { \
		dev_err(ts->dev, "%s: Error reading Reg0x%02x\n", __func__, r); \
		goto failure; \
	} \
}

#define SET_REG_IF(r, v) { \
	ret = pct1812_i2c_write(r, &(v), sizeof(v)); \
	if (ret) { \
		dev_err(ts->dev, "%s: Error writing Reg0x%02x val=0x%02x\n", __func__, r, v); \
		goto failure; \
	} \
}

static int pct1812_get_extinfo(struct pct1812_data *ts)
{
	unsigned char val, v1, v2;
	int ret;

	QUERY_PARAM(v1, BANK(0), VER_LOW_REG, val);
	QUERY_PARAM(v2, BANK(0), VER_HIGH_REG, val);
	ts->version = v1 | (v2 << 8);
	QUERY_PARAM(ts->tx_count, BANK(0), TX_REG, val);
	QUERY_PARAM(ts->rx_count, BANK(0), RX_REG, val);
	QUERY_PARAM(ts->model, BANK(0), MODEL_REG, val);
	QUERY_PARAM(ts->product_id, BANK(0), PROD_ID_REG, val);
	QUERY_PARAM(v1, BANK(0), FW_MAJOR_REG, val);
	ts->major = v1 >> 4;
	QUERY_PARAM(v2, BANK(0), FW_VMIN_L_REG, val);
	// 4bits in high byte of MINOR version come from MAJOR register
	ts->minor = v2 | ((v1 & 0x0f) << 8);
	QUERY_PARAM(ts->revision, BANK(0), FW_REV_REG, val);
	QUERY_PARAM(ts->patch, BANK(0), FW_PATCH_REG, val);
	QUERY_PARAM(v1, BANK(2), XRES_L_REG, val);
	QUERY_PARAM(v2, BANK(2), XRES_H_REG, val);
	ts->x_res = v1 | (v2 << 8);
	QUERY_PARAM(v1, BANK(2), YRES_L_REG, val);
	QUERY_PARAM(v2, BANK(2), YRES_H_REG, val);
	ts->y_res = v1 | (v2 << 8);
	QUERY_PARAM(v1, BANK(2), MAX_POINTS_REG, val);
	ts->max_points = v1 & 0x0f;

	return ret;
}

static int inline pct1812_engmode(struct pct1812_data *ts, bool on)
{
	unsigned char value;
	int ret;

	value = RESET_CODE;
	SET_PARAM(BANK(1), KEY1_REG, value);
	value = on ? ENGINEERING_CODE : RESUME_CODE;
	SET_PARAM(BANK(1), KEY2_REG, value);

	pct1812_delay_ms(2);

	return 0;
failure:
	return -EIO;
}

static int pct1812_flash_exec(struct pct1812_data *ts,
		unsigned char cmd, unsigned char flash_cmd, int cnt)
{
	unsigned char lval;
	int repetition = 0;
	int ret, step = 0;

	pr_debug("cmd 0x%02x, flash_cmd 0x%02x, cnt %d\n", cmd, flash_cmd, cnt);
#ifndef DRY_RUN_UPDATE
	lval = 0;
	SET_PARAM(BANK(4), 0x2c, lval);
	step++;

	lval = flash_cmd;
	SET_PARAM(BANK(4), 0x20, lval);
	step++;

	lval = cnt & 0xff;
	SET_PARAM(BANK(4), 0x22, lval);
	step++;

	lval = (cnt >> 8) & 0xff;
	SET_PARAM(BANK(4), 0x23, lval);
	step++;

do_again:
	lval = 0;
	SET_PARAM(BANK(4), 0x2c, lval);

	if ((lval & cmd) != 0) {
		repetition++;
		if (repetition%11)
			pr_debug("Still waiting ... %d\n", repetition);
		goto do_again;
	}
#else
	lval = 0, ret = 0, step = 0, repetition = 0;
#endif
	return 0;
#ifndef DRY_RUN_UPDATE
failure:
	dev_err(ts->dev,
		"Error exec cmd 0x%02x, flash_cmd 0x%02x, cnt %d (stage %d, reps %d)\n",
		__func__, cmd, flash_cmd, cnt, step, repetition);

	return -EIO;
#endif
}

static int pct1812_flash_status(struct pct1812_data *ts, int idx, int vok)
{
	unsigned char status[2];
	int ret;
#ifndef DRY_RUN_UPDATE
do_again:
#endif
	ret = pct1812_flash_exec(ts, 0x08, 0x05, 1);
	if (ret) {
		dev_err(ts->dev, "Error flash cmd\n", __func__);
		return -EIO;
	}
#ifndef DRY_RUN_UPDATE
	ret = pct1812_user_access(ts, BANK(4), FLASH_STATUS_REG, status, sizeof(status), AREAD);
	if (ret) {
		dev_err(ts->dev, "Error reading flash status\n", __func__);
		return -EIO;
	}

	if (status[idx] != vok) {
		pct1812_delay_ms(1); // adjust delay if necessary
		goto do_again;
	}
#else
	ret = 0, status[0] = status[1] = 0;
#endif
	pr_debug("Flash status ([%d]=%d) OK\n", idx, vok);

	return 0;
}

static int pct1812_flash_chunk(struct pct1812_data *ts, unsigned int address)
{
	int ret, step = 0;
	unsigned char value;

	// Flash WriteEnable
	ret = pct1812_flash_exec(ts, 0x02, 0x09, 0);
	if (ret)
		goto failure;
	step++;
	// check status
	ret = pct1812_flash_status(ts, 1, 1);
	if (ret)
		goto failure;
	step++;
#ifndef DRY_RUN_UPDATE
	value = (unsigned char)(address & 0xff);
	SET_PARAM(BANK(4), FLASH_ADDR0_REG, value);
	step++;

	value = (unsigned char)((address >> 8) & 0xff);
	SET_PARAM(BANK(4), FLASH_ADDR1_REG, value);
	step++;

	value = (unsigned char)((address >> 16) & 0xff);
	SET_PARAM(BANK(4), FLASH_ADDR2_REG, value);
	step++;
#else
	value = 0;
#endif
	ret = pct1812_flash_exec(ts, 0x81, 0x02, CHUNK_SZ);
	if (ret)
		goto failure;
	step++;
	// wait for completion
	ret = pct1812_flash_status(ts, 0, 0);
	if (ret)
		goto failure;

	return 0;

failure:
	dev_err(ts->dev, "Error erasing address 0x%06x (stage %d)\n",
			__func__, address, step);
	return -EIO;
}

static int pct1812_erase_sector(struct pct1812_data *ts, unsigned int address)
{
	int ret, step = 0;
	unsigned char value;

	pr_debug("Erasing sector at address 0x%06x\n", address);

	// Flash WriteEnable
	ret = pct1812_flash_exec(ts, 0x02, 0x09, 0);
	if (ret)
		goto failure;
	step++;
	// check status
	ret = pct1812_flash_status(ts, 1, 1);
	if (ret)
		goto failure;
	step++;
#ifndef DRY_RUN_UPDATE
	value = (unsigned char)(address & 0xff);
	SET_PARAM(BANK(4), FLASH_ADDR0_REG, value);
	step++;

	value = (unsigned char)((address >> 8) & 0xff);
	SET_PARAM(BANK(4), FLASH_ADDR1_REG, value);
	step++;

	value = (unsigned char)((address >> 16) & 0xff);
	SET_PARAM(BANK(4), FLASH_ADDR2_REG, value);
	step++;
#else
	value = 0;
#endif
	ret = pct1812_flash_exec(ts, 0x02, 0x20, 3);
	if (ret)
		goto failure;
	step++;
	// wait for completion
	ret = pct1812_flash_status(ts, 0, 0);
	if (ret)
		goto failure;

	return 0;

failure:
	dev_err(ts->dev, "Error erasing address 0x%06x (stage %d)\n",
			__func__, address, step);
	return -EIO;
}

static int pct1812_flash_section(struct pct1812_data *ts, unsigned char *data,
		unsigned int size, unsigned int address)
{
	int m, ret;
	unsigned int num_of_sectors = size / PCT1812_SECTOR_SIZE;
	unsigned int target_num = size / CHUNK_SZ;
	unsigned char value;
	unsigned char *ptr = data;

	for (m = 0; m < num_of_sectors; m++) {
		ret = pct1812_erase_sector(ts, address + PCT1812_SECTOR_SIZE * m);
		if (ret)
			goto failure;
		pr_debug("Erased sector %d of %d\n", m + 1, num_of_sectors);
	}

	for (m = 0; m < target_num; m++) {
#ifndef DRY_RUN_UPDATE
		// Set SRAM select
		value = 0x08;
		SET_PARAM(BANK(2), 0x09, value);
		// Set SRAM NSC to 0
		value = 0x00;
		SET_PARAM(BANK(2), 0x0a, value);
		// Write data to SRAM port
		SET_PARAM(BANK(2), 0x0b, value);
		// Set SRAM NSC to 1
		value = 0x01;
		SET_PARAM(BANK(2), 0x0a, value);
#else
		value = 0;
#endif
		// advance data pointer
		ptr += CHUNK_SZ;
		pr_debug("Flashing chunk @0x%04x\n", address);
		// Program Flash from SRAM
		ret = pct1812_flash_chunk(ts, address);
		if (ret)
			goto failure;
		address += CHUNK_SZ;
	}

	return 0;
failure:
	dev_err(ts->dev, "Error flashing chunk %d\n", __func__, m);
	return -EIO;
}

/*static*/
int pct1812_fw_update(struct pct1812_data *ts, char *fname)
{
	int ret;
	unsigned char value;
	unsigned char *fwdata_ptr, *fwparam_ptr;
	unsigned int fwdata_size, fwparam_size;
	const struct firmware *fw_entry = NULL;

	//__pm_stay_awake(&ts->wake_src);
	mutex_lock(&ts->cmdlock);
	pr_debug("Start of FW reflash process\n");
	//fwu_irq_enable(fwu, true);
	pr_debug("Requesting firmware %s\n", fname);
	ret = request_firmware(&fw_entry, fname, ts->dev);
	if (ret) {
		dev_err(ts->dev, "%s: Error loading firmware %s\n", __func__, fname);
		ret = -EINVAL;
		goto failure;
	}
	// FW file size check
	if (fw_entry->size != SUPPORTED_FW_FILE_SIZE) {
		dev_err(ts->dev, "%s: Firmware %s file size is WRONG!!!\n", __func__, fname);
		ret = -EINVAL;
		goto failure;
	}

	fwdata_size = FW_SECTION_SIZE;
	fwdata_ptr = (unsigned char *)fw_entry->data;
	fwparam_size = SUPPORTED_FW_FILE_SIZE - FW_SECTION_SIZE;
	fwparam_ptr = fwdata_ptr + FW_SECTION_SIZE;

	ret = pct1812_engmode(ts, true);
	if (ret) {
		dev_err(ts->dev, "%s: Error entering flash mode\n", __func__);
		goto failure;
	}

	value = POWERUP_CODE;
	SET_PARAM(BANK(1), FLASH_PUP_REG, value);

	ret = pct1812_flash_section(ts, fwdata_ptr, fwdata_size, ADDRESS_FW);
	if (ret) {
		dev_err(ts->dev, "%s: Flash error!!!\n", __func__);
	} else {
		ret = pct1812_flash_section(ts, fwparam_ptr, fwparam_size, ADDRESS_PARAMS);
		if (ret)
			dev_err(ts->dev, "%s: Flash error!!!\n", __func__);
	}

failure:
	ret = pct1812_engmode(ts, false);
	if (ret) {
		dev_err(ts->dev, "%s: Error leaving flash mode\n", __func__);
	}
	mutex_unlock(&ts->cmdlock);
	//__pm_relax(&ts->wake_src);

	return ret;
}

// vok - value compared bit wise
static int pct1812_wait4ready(struct pct1812_data *ts, unsigned char reg, unsigned char vok)
{
	unsigned char status = 0;
	int rlimit = CNT_WAIT_RETRY;
	int dwait = CMD_WAIT_DELAY;
	int retry, ret;

	// MODE_COMPLETE takes much longer
	if (vok == MODE_COMPLETE) {
		rlimit = 250;
		dwait = 20;
	}

	for (retry = 0; retry < rlimit; retry++) {
		ret = pct1812_i2c_read(ts, reg, &status, sizeof(status));
		if (ret || ((status & vok) == vok))
			break;
		pct1812_delay_ms(dwait);
	}
	if (retry == rlimit)
		ret = -ETIME;
	else if (!ret) {
		pr_debug("success waiting for %d\n", vok);
	}

	return (status & vok) ? 0 : ret;
}

static int pct1812_regulator(struct pct1812_data *ts, bool get)
{
	struct pct1812_platform *pdata = ts->plat_data;
	struct regulator *rvdd = NULL;
	struct regulator *rvio = NULL;

	if (get) {
		rvdd = regulator_get(ts->dev, pdata->vdd);
		if (IS_ERR_OR_NULL(rvdd)) {
			dev_err(ts->dev, "%s: Failed to get %s regulator\n",
				__func__, pdata->vdd);
			rvdd = NULL;
		}
		if (pdata->vio)
			rvio = regulator_get(ts->dev, pdata->vio);
		if (IS_ERR_OR_NULL(rvio)) {
			dev_err(ts->dev, "%s: Failed to get %s regulator.\n",
				__func__, pdata->vio);
			rvio = NULL;
		}
	} else {
		regulator_put(ts->reg_vdd);
		if (pdata->vio)
			regulator_put(ts->reg_vio);
	}

	ts->reg_vdd = rvdd;
	ts->reg_vio = rvio;

	pr_debug("vdd=%p, vio=%p\n", rvdd, rvio);

	return 0;
}

static int pct1812_pinctrl_state(struct pct1812_data *info, bool on)
{
	struct pinctrl_state *state_ptr;
	const char *state_name;
	int error = 0;

	if (!info->ts_pinctrl)
		return 0;

	if (on) {
		state_name = PINCTRL_STATE_ACTIVE;
		state_ptr =info->pinctrl_state_active;
	} else {
		state_name = PINCTRL_STATE_SUSPEND;
		state_ptr =info->pinctrl_state_suspend;
	}

	error = pinctrl_select_state(info->ts_pinctrl, state_ptr);
	if (error < 0)
		dev_err(info->dev, "%s: Failed to select %s\n",
			__func__, state_name);
	else
		pr_debug("set pinctrl state %s\n", state_name);

	return error;
}

static int pct1812_pinctrl_init(struct pct1812_data *info)
{
	int retval;

	/* Get pinctrl if target uses pinctrl */
	info->ts_pinctrl = devm_pinctrl_get(info->dev);
	if (IS_ERR_OR_NULL(info->ts_pinctrl)) {
		retval = PTR_ERR(info->ts_pinctrl);
		dev_err(info->dev, "%s Target not using pinctrl %d\n",
			__func__, retval);
		goto err_pinctrl_get;
	}

	info->pinctrl_state_active = pinctrl_lookup_state(
			info->ts_pinctrl, PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(info->pinctrl_state_active)) {
		retval = PTR_ERR(info->pinctrl_state_active);
		dev_err(info->dev, "%s Can not lookup %s pinstate %d\n",
			__func__, PINCTRL_STATE_ACTIVE, retval);
		goto err_pinctrl_lookup;
	}

	info->pinctrl_state_suspend = pinctrl_lookup_state(
			info->ts_pinctrl, PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(info->pinctrl_state_suspend)) {
		retval = PTR_ERR(info->pinctrl_state_suspend);
		dev_err(info->dev, "%s Can not lookup %s pinstate %d\n",
			__func__, PINCTRL_STATE_SUSPEND, retval);
		goto err_pinctrl_lookup;
	}

	return 0;

err_pinctrl_lookup:
	devm_pinctrl_put(info->ts_pinctrl);
err_pinctrl_get:
	info->ts_pinctrl = NULL;
	return retval;
}

static int pct1812_power(struct pct1812_data *ts, bool on)
{
	struct pct1812_platform *pdata = ts->plat_data;

	if (on) {
		if (pdata->vio)
			regulator_enable(ts->reg_vio);
		regulator_enable(ts->reg_vdd);
		pct1812_delay_ms(PON_DELAY);
	} else {
		regulator_disable(ts->reg_vdd);
		if (pdata->vio)
			regulator_disable(ts->reg_vio);
	}

	dev_info(ts->dev,"%s: power %s: %s:%s\n", __func__, on ? "ON" : "OFF",
		pdata->vdd, regulator_is_enabled(ts->reg_vdd) ? "on" : "off");
	if (pdata->vio)
		dev_info(ts->dev,"%s: %s: %s:%s\n", __func__, on ? "on" : "off",
			pdata->vio,
			regulator_is_enabled(ts->reg_vio) ? "on" : "off");

	return 0;
}
#if 0
static int pct1812_power_mode(struct pct1812_data *ts, enum pwr_modes mode)
{
	return 0;
}
#endif
static int pct1812_run_cmd(struct pct1812_data *ts, enum cmds command, unsigned char vok)
{
	unsigned char code;
	const char *action;
	int ret, counter = 0;

	switch (command) {
	case CMD_RESET:
			action = "RESET";
			code = RESET_CODE;
				break;
	case CMD_RESUME:
			action = "RESUME";
			code = RESUME_CODE;
				break;
	case CMD_SUSPEND:
			action = "SUSPEND";
			code = SUSPEND_CODE;
				break;
	case CMD_MODE_RESET:
			code = 0x06; // selftest
			ret = pct1812_i2c_write(ts, MODE_REG, &code, sizeof(code));
			if (ret) {
				dev_err(ts->dev, "%s: Mode reset error\n", __func__);
				return ret;
			}
			pct1812_delay_ms(1);
			action = "MODE_RESET";
			code = RESET_CODE;
				break;
	default: return -EINVAL;
	}

run_once_again:
	pr_debug("command: %s\n", action);
	ret = pct1812_i2c_write(ts, SW_RESET_REG, &code, sizeof(code));
	if (ret) {
		dev_err(ts->dev, "%s: Error sending cmd: %d (%d)\n", __func__, command, ret);
		return ret;
	}

	if (!counter++)
		pct1812_delay_ms(10);

	if (code == RESET_CODE) {
		action = "RESUME";
		code = RESUME_CODE;
		goto run_once_again;
	}

	return vok ? pct1812_wait4ready(ts, BOOT_STATUS_REG, vok) : 0;
}

static int pct1812_queued_resume(struct pct1812_data *ts)
{
	int ret;

	pr_debug("enter\n");

	if (atomic_cmpxchg(&ts->touch_stopped, 1, 0) == 0)
		return 0;

	ret = pct1812_run_cmd(ts, CMD_RESUME, BOOT_COMPLETE);
	if (ret) { // set active flag and irq
	}

	return ret;
}

static int pct1812_report_mode(struct pct1812_data *ts,
			unsigned char set_mask, unsigned char clear_mask,
			unsigned char *stored_mask)
{
	unsigned char MASK, val;
	int i, ret;

	QUERY_PARAM(MASK, BANK(0), INTR_MASK_REG, val);
	if (stored_mask)
		*stored_mask = MASK;
	pr_debug("Current mask 0x%02x\n", MASK);
	for (i = 0; i < 8; i++) {
		if (clear_mask & (1 << i)) {
			MASK &= ~(1 << i); // clear bit
			pr_debug("mask w/bit cleared %02x\n", MASK);
		}
		if (set_mask & (1 << i)) {
			MASK |= (1 << i);
			pr_debug("mask w/bit set %02x\n", MASK);
		}
	}
	SET_PARAM(BANK(0), INTR_MASK_REG, MASK);
	pr_debug("New interrupt mask 0x%02x\n", MASK);

failure:
	return 0;
}

// overall size of test data followed by test type
// Full Panel & SNR have Tx/Rx followed by nodes data
// Draw Line contains only data
#define SELFTEST_HDR_SZ (sizeof(unsigned int) + sizeof(unsigned char))

static int pct1812_selftest_memory(struct pct1812_data *ts, bool allocate)
{
	int ff, ret = 0;

	if (!allocate) {
		ff = ts->frame_count - 1;
		goto dealloc;
	}

	ts->test_data = kzalloc(sizeof(void *) * ts->frame_count, GFP_KERNEL);
	if (!ts->test_data)
		return -ENOMEM;

	for (ff = 0; ff < ts->frame_count; ff++) {
		ts->test_data[ff] = kzalloc(ts->frame_size, GFP_KERNEL);
		if (!ts->test_data[ff]) {
			ret = -ENOMEM;
			goto dealloc;
		}
	}
	pr_debug("allocated %d frames %d bytes each\n", ts->frame_count, ts->frame_size);

	return 0;

dealloc:
	for (; ff >= 0; ff--)
		kfree(ts->test_data[ff]);
	kfree(ts->test_data);
	ts->test_type = PCT1812_SELFTEST_NONE;
	ts->frame_count = ts->frame_size = ts->test_data_size = ts->frame_sample = 0;
	pr_debug("free-ed selftest memory\n");

	return ret;
}

static int pct1812_selftest_cleanup(struct pct1812_data *ts)
{
	int ret = 0;

	switch (ts->test_type) {
	case PCT1812_SELFTEST_DRAW_LINE:
			ret = pct1812_run_cmd(ts, CMD_RESET, BOOT_COMPLETE);
			if (ret) {
				dev_err(ts->dev, "%s: Reset failed\n", __func__);
			}
			// no need to restore intr mask; reset done it already!!!
			//pct1812_report_mode(ts, ts->test_intr_mask, 0xff, NULL);
			pct1812_drop_mask_set(ts, STAT_BIT_TOUCH);
	default:
			pct1812_selftest_set(ts, PCT1812_SELFTEST_NONE);
			pct1812_selftest_memory(ts, false);
	}

	return ret;
}

#define NANO_SEC 1000000000
#define SEC_TO_MSEC 1000
#define NANO_TO_MSEC 1000000

static inline unsigned int timediff_ms(struct pct1812_data *ts)
{
	struct timespec64 start = ktime_to_timespec64(ts->test_start_time);
	struct timespec64 end = ktime_to_timespec64(ktime_get());
	struct timespec64 temp;
	unsigned int diff_ms;

	if ((end.tv_nsec - start.tv_nsec) < 0) {
		temp.tv_sec = end.tv_sec - start.tv_sec - 1;
		temp.tv_nsec = NANO_SEC + end.tv_nsec - start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec - start.tv_sec;
		temp.tv_nsec = end.tv_nsec - start.tv_nsec;
	}
	diff_ms = (unsigned int)((temp.tv_sec * SEC_TO_MSEC) + (temp.tv_nsec / NANO_TO_MSEC));
	pr_debug("timediff %ums\n", diff_ms);

	return diff_ms;
}

static void pct1812_work(struct work_struct *work)
{
	struct pct1812_data *ts = container_of(work, struct pct1812_data, worker.work);
	unsigned char status = 0;
	static unsigned char prev = 1;
	const char *action = NULL;
	int mode, cmd, ret = 0;
	int rearm_cmd = CMD_WDOG;
	unsigned int duration;

	while (kfifo_get(&ts->cmd_pipe, &cmd)) {
		mode = pct1812_selftest_get(ts);
		//pr_debug("cmd = %d, mode = %d\n", cmd, mode);
		switch (cmd) {
		case CMD_CLEANUP:
				duration = timediff_ms(ts);
				if (mode == PCT1812_SELFTEST_IN_PROGRESS ||
					duration < SELFTEST_EXTRA_LONG_INTERVAL) {
					rearm_cmd = CMD_CLEANUP;
					action = "CLEANUP_POSTPONED";
				} else if (mode != PCT1812_SELFTEST_NONE) {
					action = "SELFTEST_CLEANUP";
					pct1812_selftest_cleanup(ts);
				}
					break;
		case CMD_WDOG:
				if (mode == PCT1812_SELFTEST_NONE) {
					action = "WDOG";
#ifndef CALM_WDOG
					ret = pct1812_i2c_read(ts, FRAME_NUM_REG, &status, sizeof(status));
#endif
					if (ret || (prev == status)) {
						dev_warn(ts->dev, "%s: Possible lockup\n", __func__);
					}
#ifndef CALM_WDOG
					/* update frame counter */
					prev = status;
#endif
				} else {
					action = "WDOG_POSTPONED";
				}
					break;
		case CMD_RECOVER:
				action = "RECOVER";
				ret = pct1812_run_cmd(ts, CMD_RESET, BOOT_COMPLETE);
				if (ret) {
						dev_err(ts->dev, "%s: Reset failed\n", __func__);
				}
					break;
		case CMD_RESUME:
				action = "RESUME";
				ret = pct1812_queued_resume(ts);
				if (ret)
						dev_err(ts->dev, "%s: Failed to resume\n", __func__);
					break;
		}
	}

	if (action && strcmp(action, "WDOG"))
		pr_debug("action: %s\n", action);

	pct1812_fifo_cmd_add(ts, rearm_cmd, WDOG_INTERVAL);
}
#if 0
static int pct1812_suspend(struct device *dev)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	int ret;

	pr_debug("enter\n");

	if (atomic_cmpxchg(&ts->touch_stopped, 0, 1) == 1)
		return 0;

	ret = pct1812_run_cmd(ts, CMD_SUSPEND, 0);
	if (!ret) { // set active flag and irq
	}

	return ret;
}

static int pct1812_resume(struct device *dev)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);

	//atomic_set(&ts->resume_should_stop, 0);
	kfifo_put(&ts->cmd_pipe, CMD_RESUME);
	/* schedule_delayed_work returns true if work has been scheduled */
	/* and false otherwise, thus return 0 on success to comply POSIX */
	return schedule_delayed_work(&ts->worker, 0) == false;
}
#endif

static int pct1812_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct pct1812_data *ts;
	struct pct1812_platform *pdata;
	int ret = 0;

 	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "%s: EIO err!!!\n", __func__);
		return -EIO;
	}

	ts = kzalloc(sizeof(struct pct1812_data), GFP_KERNEL);
	if (!ts)
		goto error_allocate_mem;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "%s: Failed to locate dt\n", __func__);
		goto error_allocate_mem;
	}

	pdata = devm_kzalloc(&client->dev, sizeof(struct pct1812_platform), GFP_KERNEL);
	if (!pdata) {
		dev_err(&client->dev, "%s: Failed to allocate platform data\n", __func__);
		goto error_allocate_pdata;
	}

	client->dev.platform_data = pdata;
	pdata->client = client;

#if defined(STATIC_PLATFORM_DATA)
	memcpy(pdata, (const void *)&pct1812_pd, sizeof(struct pct1812_platform));
#else
	ret = pct1812_parse_dt(client);
	if (ret) {
		dev_err(&client->dev, "%s: Failed to parse dt\n", __func__);
		goto error_allocate_mem;
	}
#endif

	ret = pct1812_platform_init(pdata);
	if (ret) {
		dev_err(&client->dev, "%s: Failed to init platform\n", __func__);
		goto error_allocate_mem;
	}

 	ts->client = client;
	ts->dev = &client->dev;
	ts->plat_data = pdata;

	i2c_set_clientdata(client, ts);
	dev_set_drvdata(&client->dev, ts);

	mutex_init(&ts->i2c_mutex);
	mutex_init(&ts->cmdlock);
	mutex_init(&ts->eventlock);

	INIT_DELAYED_WORK(&ts->worker, pct1812_work);

	pct1812_pinctrl_init(ts);
	pct1812_pinctrl_state(ts, true);

	pct1812_regulator(ts, true);
	pct1812_power(ts, true);

	pct1812_delay_ms(TOUCH_RESET_DELAY);
	ret = pct1812_wait4ready(ts, BOOT_STATUS_REG, BOOT_COMPLETE);
	if (ret == -ETIME) {
		dev_err(&client->dev, "%s: Failed to init\n", __func__);
		goto error_init;
	}

	pct1812_drop_mask_set(ts, STAT_BIT_TOUCH);
	pct1812_get_extinfo(ts);

	ret = kfifo_alloc(&ts->cmd_pipe, sizeof(unsigned int)* 10, GFP_KERNEL);
	if (ret)
		goto error_init;

	ret = request_threaded_irq(client->irq, NULL, pct1812_irq_handler,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT, MYNAME, ts);
	if (ret < 0) {
		dev_err(&client->dev, "%s: Unable to request threaded irq\n", __func__);
		goto error_fifo_alloc;
	}

	/* prevent unbalanced irq enable */
	ts->irq_enabled = true;

	pct1812_fifo_cmd_add(ts, CMD_WDOG, WDOG_INTERVAL);

	ret = sysfs_create_group(&client->dev.kobj, &pct1812_attrs_group);
	if (ret)
		dev_warn(&client->dev, "%s: Error creating sysfs entries %d\n", __func__, ret);

	sysfs_create_bin_file(&client->dev.kobj, &selftest_bin_attr);

	return 0;

error_fifo_alloc:
	kfifo_free(&ts->cmd_pipe);

error_init:
	pct1812_power(ts, false);
	pct1812_regulator(ts, false);

error_allocate_mem:
error_allocate_pdata:

	dev_err(&client->dev, "%s: failed(%d)\n", __func__, ret);

	return ret;
}

static ssize_t ic_ver_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%04x;tx/rx:%d/%d", ts->version, ts->tx_count, ts->rx_count);
}

static ssize_t vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "pixart");
}

static ssize_t irq_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	int value = gpio_get_value(ts->plat_data->irq_gpio);

	return scnprintf(buf, PAGE_SIZE, "%s", value ? "High" : "Low");
}

static ssize_t drv_irq_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d", ts->irq_enabled ? 1 : 0);
}

static ssize_t drv_irq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	unsigned long value = 0;
	int ret = 0;

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0) {
		dev_err(ts->dev, "%s: Conversion failed\n", __func__);
		return -EINVAL;
	}

	pct1812_set_irq(ts, !!(value));

	return size;
}

static int pct1812_raw_data_frame(struct pct1812_data *ts,
		unsigned char *buffer)
{
	unsigned char cmd, tx, rx;
	int ret = 0;

	cmd = 0x06; // selftest
	ret = pct1812_i2c_write(ts, MODE_REG, &cmd, sizeof(cmd));
	if (ret)
		goto failure;

	QUERY_PARAM_IF(tx, BANK(0), 0x5a, cmd);
	QUERY_PARAM_IF(rx, BANK(0), 0x59, cmd);
	dev_info(ts->dev, "%s: Tx/Rx=%d/%d\n", __func__, tx, rx);

	cmd = 0x01; // selftest
	ret = pct1812_i2c_write(ts, MODE_REG, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x40;
	ret = pct1812_i2c_write(ts, 0x0d, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x06;
	ret = pct1812_i2c_write(ts, 0x0e, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x02;
	ret = pct1812_i2c_write(ts, MODE_REG, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x05;
	ret = pct1812_i2c_write(ts, 0x09, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x0;
	ret = pct1812_i2c_write(ts, 0x0a, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	// read data from SRAM port
	ret = pct1812_i2c_read(ts, 0x0b, buffer, ts->frame_size);
	if (ret)
		goto failure;
	cmd = 0x1;
	ret = pct1812_i2c_write(ts, 0x0a, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x01; // selftest
	ret = pct1812_i2c_write(ts, MODE_REG, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x0;
	ret = pct1812_i2c_write(ts, 0x0d, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	ret = pct1812_i2c_write(ts, 0x0e, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x06; // selftest
	ret = pct1812_i2c_write(ts, MODE_REG, &cmd, sizeof(cmd));
	if (ret)
		goto failure;
	cmd = 0x00; // clear interrupt
	ret = pct1812_i2c_write(ts, STATUS_REG, &cmd, sizeof(cmd));
	if (ret)
		goto failure;

	return 0;

failure:
	dev_err(ts->dev, "%s: Read error\n", __func__);

	return -EIO;
}

static unsigned char *selftest_data_ptr(struct pct1812_data *ts,
		unsigned int offset, unsigned int need2read, unsigned int *avail)
{
	int ff;
	unsigned char *bptr = NULL;

	for (ff = 0; ff < ts->frame_count; ff++) {
		if (offset >= ts->frame_size) {
			offset -= ts->frame_size;
			continue;
		}
		bptr = ts->test_data[ff];
		bptr += offset;
		if (need2read < (ts->frame_size - offset))
			*avail = need2read;
		else
			*avail = ts->frame_size - offset;
		break;
	}

	return bptr;
}

static ssize_t selftest_bin_show(struct file *filp, struct kobject *kobp,
		struct bin_attribute *bin_attr, char *buf, loff_t pos, size_t count)
{
	int mode;
	unsigned char *bptr;
	unsigned int remain, available;
	unsigned int bOff = 0;
	unsigned int dOffset = 0;
	struct i2c_client *client = kobj_to_i2c_client(kobp);
	struct pct1812_data *ts = i2c_get_clientdata(client);

	if (!ts || pos < 0)
		return -EINVAL;

	mode = pct1812_selftest_get(ts);
	if (mode == PCT1812_SELFTEST_NONE || ts->test_data_size == 0) {
		dev_warn(ts->dev, "%s: No selftest results available\n", __func__);
		return 0;
	}

	if (pos >= ts->test_data_size) {
		dev_warn(ts->dev, "%s: Position %lu beyond data boundary of %u\n",
				__func__, pos, ts->test_data_size);
		return 0;
	}

	if (count > ts->test_data_size - pos)
		count = ts->test_data_size - pos;
	/* available data size */
	remain = count;

	if (pos == 0) {
		pr_debug("sending selftest data header\n");
		memcpy(buf, &ts->test_data_size, sizeof(unsigned int));
		bOff += sizeof(unsigned int);
		memcpy(buf + bOff, &ts->test_type, sizeof(unsigned char));
		bOff += sizeof(unsigned char);
		if (ts->test_type != PCT1812_SELFTEST_DRAW_LINE) {
			memcpy(buf + bOff, &ts->tx_count, sizeof(unsigned char));
			bOff += sizeof(unsigned char);
			memcpy(buf + bOff, &ts->rx_count, sizeof(unsigned char));
			bOff += sizeof(unsigned char);
		}
		/* available data size after sending header */
		remain -= bOff;
	} else if (pos >= ts->test_hdr_size) {
		/* offset within data buffer (excludes header) */
		dOffset = pos - ts->test_hdr_size;
	}

	pr_debug("start at offset %u, remaining %u\n", dOffset, remain);
	while ((bptr = selftest_data_ptr(ts, dOffset, remain, &available)) &&
				available > 0) {
		pr_debug("available for copying %u bytes at offset %u\n", available, dOffset);
		memcpy(buf + bOff, bptr, available);
		bOff += available;
		dOffset += available;
		remain -= available;
	}

	if ((pos + count) >= ts->test_data_size) {
		pct1812_selftest_cleanup(ts);
	}

	return count;
}

static ssize_t pct1812_print_raw_data(struct pct1812_data *ts, char *buf)
{
	unsigned char *bptr;
	int ival, ll, rr, cc;
	ssize_t blen = 0;

	blen = scnprintf(buf, PAGE_SIZE, "%08x;%02x;%02x,%02x\n",
				ts->test_data_size, ts->test_type, ts->tx_count, ts->rx_count);
	for (ll = 0; ll < ts->frame_count; ll++) {
		blen += scnprintf(buf + blen, PAGE_SIZE - blen,
								"       Tx0   Tx1   Tx2   Tx3   Tx4   Tx5\n");
		bptr = ts->test_data[ll];
		for (rr = 0; rr < ts->rx_count; rr++) {
			blen += scnprintf(buf + blen, PAGE_SIZE - blen, "Rx%d: ", rr);
			for (cc = 0; cc < ts->tx_count; cc++) {
				pr_debug("offset %d\n", (int)(bptr - ts->test_data[ll]));
				ival = comp2_16b(bptr);
				bptr += 2;
				blen += scnprintf(buf + blen, PAGE_SIZE - blen, "%5d ", ival);
			}
			blen += scnprintf(buf + blen, PAGE_SIZE - blen, "\n");
		}
	}

	return blen;
}

static ssize_t pct1812_print_coords(struct pct1812_data *ts, char *buf)
{
	int ss;
	ssize_t blen = 0;

	blen = scnprintf(buf, PAGE_SIZE, "%08x;%02x\n", ts->test_data_size, ts->test_type);
	for (ss = 0; ss < ts->frame_sample; ss++) {
		blen += scnprintf(buf + blen, PAGE_SIZE - blen,
					"%5d,%5d;\n", UINT16_X(ts->test_data[ss]), UINT16_Y(ts->test_data[ss]));
	}

	return blen;
}

static ssize_t selftest_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	int mode;
	ssize_t blen = 0;

	mode = pct1812_selftest_get(ts);
	if (mode == PCT1812_SELFTEST_IN_PROGRESS ||
				ts->test_type == PCT1812_SELFTEST_NONE ||
				ts->test_data_size == 0) {
		dev_warn(ts->dev, "%s: No selftest results available\n", __func__);
		return blen;
	}

	pr_debug("%d frame(s) available\n", ts->frame_count);
	if (ts->test_type == PCT1812_SELFTEST_FULL ||
				ts->test_type == PCT1812_SELFTEST_SNR)
		blen = pct1812_print_raw_data(ts, buf);
	else
		blen = pct1812_print_coords(ts, buf);

	pct1812_selftest_cleanup(ts);

	return blen;
}

static ssize_t selftest_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	unsigned long value = 0;
	unsigned char *test, cmd;
	int mode, ret = 0;
	int ll;

	mode = pct1812_selftest_get(ts);
	if (mode != PCT1812_SELFTEST_NONE) {
		dev_err(ts->dev, "%s: Selftest in progress\n", __func__);
		return -EBUSY;
	}

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0) {
		dev_err(ts->dev, "%s: Conversion failed\n", __func__);
		return -EINVAL;
	} else if (value <= PCT1812_SELFTEST_NONE || value >= PCT1812_SELFTEST_MAX) {
		dev_err(ts->dev, "%s: Invalid selftest id %lu\n", __func__, value);
		return -EINVAL;
	}

	ts->test_type = mode = value & 0xff;
	switch (mode) {
	case PCT1812_SELFTEST_FULL:
			ts->frame_size = ts->tx_count * ts->rx_count * sizeof(unsigned char) * 2;
			ts->frame_count = 1;
			ts->test_hdr_size = SELFTEST_HDR_SZ + sizeof(unsigned char) * 2;				
			ts->test_data_size = ts->frame_count * ts->frame_size + ts->test_hdr_size;
					break;
	case PCT1812_SELFTEST_SNR:
			ts->frame_size = ts->tx_count * ts->rx_count * sizeof(unsigned char) * 2;
			ts->frame_count = SNR_LOOP_COUNT;
			ts->test_hdr_size = SELFTEST_HDR_SZ + sizeof(unsigned char) * 2;				
			ts->test_data_size = ts->frame_count * ts->frame_size + ts->test_hdr_size;
					break;
	case PCT1812_SELFTEST_DRAW_LINE:
			ts->frame_size = sizeof(unsigned char) * 4;
			ts->frame_count = 100; // alloc memory for 100 samples
			ts->test_hdr_size = SELFTEST_HDR_SZ;				
			ts->test_data_size = ts->test_hdr_size;
					break;
	}

	ret = pct1812_selftest_memory(ts, true);
	if (ret)
		return -ENOMEM;

	switch (mode) {
	case PCT1812_SELFTEST_FULL:
			ts->test_type = PCT1812_SELFTEST_FULL;
			pct1812_selftest_set(ts, PCT1812_SELFTEST_FULL);
			test = "SELFTEST_FULL";
					break;
	case PCT1812_SELFTEST_SNR:
			ts->test_type = PCT1812_SELFTEST_SNR;
			pct1812_selftest_set(ts, PCT1812_SELFTEST_SNR);
			test = "SELFTEST_SNR";
					break;
	case PCT1812_SELFTEST_DRAW_LINE:
			ts->test_type = PCT1812_SELFTEST_DRAW_LINE;
			pct1812_selftest_set(ts, PCT1812_SELFTEST_DRAW_LINE);
			ts->test_start_time = ktime_get();
			ret = pct1812_run_cmd(ts, CMD_RESET, BOOT_COMPLETE);
			if (ret) {
				dev_err(ts->dev, "%s: Failed to reset\n", __func__);
				goto failure;
			}
			pct1812_report_mode(ts, STAT_BIT_EABS, 0xff, &ts->test_intr_mask);
			pct1812_drop_mask_set(ts, STAT_BIT_GESTURE);
			// arm timed out cleanup
			pct1812_fifo_cmd_add(ts, CMD_CLEANUP, SELFTEST_EXTRA_LONG_INTERVAL);
					return size;
	}

	// disable IRQ
	pct1812_set_irq(ts, false);
	// RESET
	ret = pct1812_run_cmd(ts, CMD_MODE_RESET, MODE_COMPLETE);
	if (ret) {
		dev_err(ts->dev, "%s: Failed to reset\n", __func__);
		goto failure;
	}
	// INITIAL
	cmd = 0x06; // selftest
	ret = pct1812_i2c_write(ts, MODE_REG, &cmd, sizeof(cmd));
	if (ret) {
		dev_err(ts->dev, "%s: Pre-initial error\n", __func__);
		goto failure;
	}
	cmd = 0x00;
	ret = pct1812_user_access(ts, BANK(0), 0x08, &cmd, sizeof(cmd), AWRITE);
	if (ret) {
		dev_err(ts->dev, "%s: Write error: bank=0, addr=0x08, value=0x00\n", __func__);
		goto failure;
	}
	cmd = 0x01;
	ret = pct1812_user_access(ts, BANK(0), 0x15, &cmd, sizeof(cmd), AWRITE);
	if (ret) {
		dev_err(ts->dev, "%s: Write error: bank=0, addr=0x15, value=0x01\n", __func__);
		goto failure;
	}
	cmd = 0x00;
	ret = pct1812_user_access(ts, BANK(1), 0x90, &cmd, sizeof(cmd), AWRITE);
	if (ret) {
		dev_err(ts->dev, "%s: Write error: bank=1, addr=0x90, value=0x00\n", __func__);
		goto failure;
	}
	// SNR selftest requires an extra step
	if ((value & 0xff) == PCT1812_SELFTEST_SNR) {
		cmd = 0x0f;
		ret = pct1812_user_access(ts, BANK(1), 0x91, &cmd, sizeof(cmd), AWRITE);
		if (ret) {
			dev_err(ts->dev, "%s: Write error: bank=1, addr=0x91, value=0x0f\n", __func__);
			goto failure;
		}
	}
	cmd = 0x00;
	ret = pct1812_user_access(ts, BANK(0), 0x2d, &cmd, sizeof(cmd), AWRITE);
	if (ret) {
		dev_err(ts->dev, "%s: Write error: bank=0, addr=0x08, value=0x00\n", __func__);
		goto failure;
	}
	cmd = 0x01;
	ret = pct1812_user_access(ts, BANK(0), 0x08, &cmd, sizeof(cmd), AWRITE);
	if (ret) {
		dev_err(ts->dev, "%s: Write error: bank=0, addr=0x08, value=0x01\n", __func__);
		goto failure;
	}

	pct1812_selftest_set(ts, PCT1812_SELFTEST_IN_PROGRESS);
	dev_info(ts->dev, "%s: Performing %s\n", __func__, test);
	for (ll = 0; ll < ts->frame_count; ll++) {
		ret = pct1812_raw_data_frame(ts, ts->test_data[ll]);
		if (ret) {
			dev_err(ts->dev, "%s: Error reading frame %d\n", __func__, ll);
			break;
		}
	}
	/* reset in case of failed selftest data collection */
	if (ret)
		goto failure;

	/* restore selftest type */
	pct1812_selftest_set(ts, mode);
	ret = pct1812_run_cmd(ts, CMD_RESET, BOOT_COMPLETE);
	if (ret)
		dev_err(ts->dev, "%s: Reset error\n", __func__);
	// enable IRQ
	pct1812_set_irq(ts, true);

	return size;

failure:
	ret = pct1812_run_cmd(ts, CMD_RESET, BOOT_COMPLETE);
	if (ret)
		dev_err(ts->dev, "%s: Reset error\n", __func__);
	pct1812_selftest_set(ts, PCT1812_SELFTEST_NONE);
	// enable IRQ
	pct1812_set_irq(ts, true);

	return size;
}

static ssize_t doreflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	char fname[64];
	int ret = 0;

	if (size >= sizeof(fname)) {
		dev_err(ts->dev, "%s: File name %d too long (max %d)\n", __func__, size, sizeof(fname));
		return -EINVAL;
	}

	strncpy(fname, buf, sizeof(fname));
	if (fname[size - 1] == '\n')
		fname[size - 1] = 0;

	ret = pct1812_fw_update(ts, fname);
	if (ret)
		dev_err(ts->dev, "%s: FW update failed\n", __func__);

	return ret ? -EIO : size;
}

static ssize_t forcereflash_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static ssize_t reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	unsigned long value = 0;
	enum cmds cmd;
	unsigned char vok;
	int ret = 0;

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0) {
		dev_err(ts->dev, "%s: Conversion failed\n", __func__);
		return -EINVAL;
	}else if (value == 0) {
		cmd = CMD_MODE_RESET;
		vok = MODE_COMPLETE;
	} else {
		cmd = CMD_RESET;
		vok = BOOT_COMPLETE;
	}

	dev_info(ts->dev, "%s: Performing reset %s\n", __func__, cmd == CMD_MODE_RESET ? "MODE" : "HW");

	ret = pct1812_run_cmd(ts, cmd, vok);
	if (ret)
		dev_err(ts->dev, "%s: Incomplete reset\n", __func__);

	return size;
}

static ssize_t info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	ssize_t blen;

	blen = scnprintf(buf, PAGE_SIZE, "   Product: %sPixArt %s\n",
				ts->model == 0x47 ? "" : "Non-",
				ts->product_id == 0x8f ? "PCT1812FF" : "unknown");
	blen += scnprintf(buf + blen, PAGE_SIZE - blen, "    FW ver: %d.%d\n", ts->major, ts->minor);
	blen += scnprintf(buf + blen, PAGE_SIZE - blen, "    FW rev: %d patch %d\n", ts->revision, ts->patch);
	blen += scnprintf(buf + blen, PAGE_SIZE - blen, "     Tx/Rx: %dx%d\n", ts->tx_count, ts->rx_count);
	blen += scnprintf(buf + blen, PAGE_SIZE - blen, "Resolution: %dx%d\n", ts->x_res, ts->y_res);
	blen += scnprintf(buf + blen, PAGE_SIZE - blen, "Max points: %d\n", ts->max_points);

	return blen;
}

static ssize_t mask_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	long value = 0;
	unsigned char set_mask, clear_mask;
	int ret;

	ret = kstrtol(buf, 10, &value);
	if (ret < 0) {
		dev_err(ts->dev, "%s: Conversion failed\n", __func__);
		return -EINVAL;
	} else if (value < -255 || value > 255) {
		dev_err(ts->dev, "%s: Value %ld is out of range [-255,255]\n", __func__);
		return -EINVAL;
	}

	if (value < 0) {
		value = -value;
		clear_mask = value & 0xff;
		set_mask = 0;
	} else {
		set_mask = value & 0xff;
		clear_mask = 0;
	}

	ret = pct1812_report_mode(ts, set_mask, clear_mask, NULL);
	if (ret)
		dev_err(ts->dev, "%s: Intr mask set error\n", __func__);

	return ret ? -EIO: size;
}

static ssize_t mask_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pct1812_data *ts = dev_get_drvdata(dev);
	unsigned char mask = 0;
	unsigned char val;
	int ret;
	QUERY_PARAM(mask, BANK(0), INTR_MASK_REG, val);
	return scnprintf(buf, PAGE_SIZE, "%02x\n", mask);
}

static const struct i2c_device_id pct1812_id[] = {
	{ MYNAME, 0 },
	{ },
};

static const struct of_device_id pct1812_match_table[] = {
	{ .compatible = "pixart,pct1812_ts",},
	{ },
};

static struct i2c_driver pct1812_driver = {
        .driver = {
                .owner = THIS_MODULE,
                .name = MYNAME,
                .of_match_table = pct1812_match_table,
        },
        .probe = pct1812_probe,
        .id_table = pct1812_id,
};

module_i2c_driver(pct1812_driver);

MODULE_AUTHOR("Konstantin Makariev <kmakariev@lenovo.com>");
MODULE_DESCRIPTION("I2C Driver for PixArt Imaging PCT1812FF");
MODULE_LICENSE("GPL v2");