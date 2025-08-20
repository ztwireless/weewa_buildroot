// SPDX-License-Identifier: GPL-2.0
/*
 * imx678 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04
 *	1.add parse mclk pinctrl.
 *	2.add set flip ctrl.
 * V0.0X01.0X05 add quick stream on/off
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/rk-preisp.h>

#define INNOSZ_WEEWA_DRIVER


#define IMX678_DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x05)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define IMX678_LINK_FREQ_445		445500000 

#define IMX678_LANES			4

#define PIXEL_RATE_WITH_445M_10BIT	(IMX678_LINK_FREQ_445 * 2 / 10 * 4)

#define IMX678_XVCLK_FREQ_37		74250000 

#define IMX678_CHIP_ID				0x4c04
#define IMX678_REG_CHIP_ID		0x302c

#define IMX678_REG_CTRL_MODE		0x3000
#define IMX678_MODE_SW_STANDBY		0x1
#define IMX678_MODE_STREAMING		0x0

#define imx678_REG_MARSTER_MODE		0x3002
#define imx678_MODE_STOP		BIT(0)
#define imx678_MODE_START		0x0


#define IMX678_SHR_EXPO_REG_H		0x3052
#define IMX678_SHR_EXPO_REG_M		0x3051
#define IMX678_SHR_EXPO_REG_L		0x3050

#define	IMX678_EXPOSURE_MIN		5
#define	IMX678_EXPOSURE_STEP		1
#define IMX678_VTS_MAX			0xfffff
#define IMX678_REG_GAIN			0x3070
#define IMX678_GAIN_MIN			0x00
#define IMX678_GAIN_MAX			0xf0
#define IMX678_GAIN_STEP		1
#define IMX678_GAIN_DEFAULT		0x30

#define IMX678_REG_TEST_PATTERN	0x5e00
#define	IMX678_TEST_PATTERN_ENABLE	0x80
#define	IMX678_TEST_PATTERN_DISABLE	0x0

#define IMX678_REG_VTS_H		0x302a
#define IMX678_REG_VTS_M		0x3029
#define IMX678_REG_VTS_L		0x3028

#define IMX678_FETCH_EXP_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX678_FETCH_EXP_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX678_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define IMX678_FETCH_RHS1_H(VAL)	(((VAL) >> 16) & 0x0F)
#define IMX678_FETCH_RHS1_M(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX678_FETCH_RHS1_L(VAL)	((VAL) & 0xFF)

#define IMX678_FETCH_VTS_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX678_FETCH_VTS_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX678_FETCH_VTS_L(VAL)		((VAL) & 0xFF)

#define IMX678_VREVERSE_REG	0x3021
#define IMX678_HREVERSE_REG	0x3020

#define IMX678_REG_DELAY			0xFFFE
#define IMX678_REG_NULL			0xFFFF

#define IMX678_REG_VALUE_08BIT		1
#define IMX678_REG_VALUE_16BIT		2
#define IMX678_REG_VALUE_24BIT		3

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"


#define IMX678_NAME			"imx678"


static const char * const imx678_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX678_NUM_SUPPLIES ARRAY_SIZE(imx678_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct imx678_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *global_reg_list;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vclk_freq;
	u32 bpp;
	u32 mipi_freq_idx;
	u32 vc[PAD_MAX];
};

struct imx678 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX678_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*test_pattern;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct imx678_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	enum rkmodule_sync_mode	sync_mode;
	
	
	u32			cur_vts;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u32			cur_vclk_freq;
	u32			cur_mipi_freq_idx;
};

#define to_imx678(sd) container_of(sd, struct imx678, subdev)

static const struct regval imx678_10_3840x2160_global_regs[] = {
	{0x3000,0x01},
	{0x3001,0x00},
	{0x3002,0x01},
	{0x3014,0x00},// ADBIT[0]	
	{0x3015,0x05},// INCKSEL 1[8:0]
	{0x3022,0x00},// INCKSEL2[1:0]
	{0x3023,0x00},
	{0x302C,0x4c},// INCKSEL4[1:0]
	{0x302D,0x04},// MDBIT
	{0x3050,0x03},// XVS_DRV[1:0]
	{0x30A6,0x00},// -
	{0x3460,0x22},// -
	{0x355A,0x64},// -
	{0x3A02,0x7A},// -
	{0x3A10,0xEC},//
	{0x3A12,0x71},// -
	{0x3A14,0xDE},// -
	{0x3A20,0x2B},// -
	{0x3A24,0x22},// -
	{0x3A25,0x25},// -
	{0x3A26,0x2A},// -
	{0x3A27,0x2C},// -
	{0x3A28,0x39},// -
	{0x3A29,0x38},// -
	{0x3A30,0x04},// -
	{0x3A31,0x04},// -
	{0x3A32,0x03},// -
	{0x3A33,0x03},// -
	{0x3A34,0x09},// -
	{0x3A35,0x06},// -
	{0x3A38,0xCD},// -
	{0x3A3A,0x4C},// -
	{0x3A3C,0xB9},// -
	{0x3A3E,0x30},// -
	{0x3A40,0x2C},// -
	{0x3A42,0x39},// -
	{0x3A4E,0x00},// -
	{0x3A52,0x00},// -
	{0x3A56,0x00},// -
	{0x3A5A,0x00},// -
	{0x3A5E,0x00},// -
	{0x3A62,0x00},// -
	{0x3A6E,0xA0},// -
	{0x3A70,0x50},// -
	{0x3A8C,0x04},// -
	{0x3A8D,0x03},// -
	{0x3A8E,0x09},// -
	{0x3A90,0x38},// -
	{0x3A91,0x42},// -
	{0x3A92,0x3C},// -
	{0x3B0E,0xF3},// -
	{0x3B12,0xE5},// -
	{0x3B27,0xC0},// -
	{0x3B2E,0xEF},// -
	{0x3B30,0x6A},// -
	{0x3B32,0xF6},// -
	{0x3B36,0xE1},// -
	{0x3B3A,0xE8},// -
	{0x3B5A,0x17},// -
	{0x3B5E,0xEF},// -
	{0x3B60,0x6A},// -
	{0x3B62,0xF6},// -
	{0x3B66,0xE1},// -
	{0x3B6A,0xE8},// -
	{0x3B88,0xEC},// -
	{0x3B8A,0xED},// -
	{0x3B94,0x71},// -
	{0x3B96,0x72},// -
	{0x3B98,0xDE},// -
	{0x3B9A,0xDF},// -
	{0x3C0F,0x06},// -
	{0x3C10,0x06},// -
	{0x3C11,0x06},// -
	{0x3C12,0x06},// -
	{0x3C13,0x06},// -
	{0x3C18,0x20},// -
	{0x3C3A,0x7A},// -
	{0x3C40,0xF4},
	{0x3C48,0xE6},
	{0x3C54,0xCE},
	{0x3C56,0xD0},
	{0x3C6C,0x53},
	{0x3C6E,0x55},
	{0x3C70,0xC0},
	{0x3C72,0xC2},
	{0x3C7E,0xCE},
	{0x3C8C,0xCF},
	{0x3C8E,0xEB},
	{0x3C98,0x54},
	{0x3C9A,0x70},
	{0x3C9C,0xC1},
	{0x3C9E,0xDD},
	{0x3CB0,0x7A},
	{0x3CB2,0xBA},
	{0x3CC8,0xBC},
	{0x3CCA,0x7C},
	{0x3CD4,0xEA},
	{0x3CD5,0x01},
	{0x3CD6,0x4A},
	{0x3CD8,0x00},
	{0x3CD9,0x00},
	{0x3CDA,0xFF},
	{0x3CDB,0x03},
	{0x3CDC,0x00},
	{0x3CDD,0x00},
	{0x3CDE,0xFF},
	{0x3CDF,0x03},
	{0x3CE4,0x4C},
	{0x3CE6,0xEC},
	{0x3CE7,0x01},
	{0x3CE8,0xFF},
	{0x3CE9,0x03},
	{0x3CEA,0x00},
	{0x3CEB,0x00},
	{0x3CEC,0xFF},
	{0x3CED,0x03},
	{0x3CEE,0x00},
	{0x3CEF,0x00},
	{0x3E28,0x82},
	{0x3E2A,0x80},
	{0x3E30,0x85},
	{0x3E32,0x7D},
	{0x3E5C,0xCE},
	{0x3E5E,0xD3},
	{0x3E70,0x53},
	{0x3E72,0x58},
	{0x3E74,0xC0},
	{0x3E76,0xC5},
	{0x3E78,0xC0},
	{0x3E79,0x01},
	{0x3E7A,0xD4},
	{0x3E7B,0x01},
	{0x3EB4,0x0B},
	{0x3EB5,0x02},
	{0x3EB6,0x4D},
	{0x3EEC,0xF3},
	{0x3EEE,0xE7},
	{0x3F01,0x01},
	{0x3F24,0x10},
	{0x3F28,0x2D},
	{0x3F2A,0x2D},
	{0x3F2C,0x2D},
	{0x3F2E,0x2D},
	{0x3F30,0x23},
	{0x3F38,0x2D},
	{0x3F3A,0x2D},
	{0x3F3C,0x2D},
	{0x3F3E,0x28},
	{0x3F40,0x1E},
	{0x3F48,0x2D},
	{0x3F4A,0x2D},
	{0x4004,0xE4},
	{0x4006,0xFF},
	{0x4018,0x69},
	{0x401A,0x84},
	{0x401C,0xD6},
	{0x401E,0xF1},
	{0x4038,0xDE},
	{0x403A,0x00},
	{0x403B,0x01},
	{0x404C,0x63},
	{0x404E,0x85},
	{0x4050,0xD0},
	{0x4052,0xF2},
	{0x4108,0xDD},
	{0x410A,0xF7},
	{0x411C,0x62},
	{0x411E,0x7C},
	{0x4120,0xCF},
	{0x4122,0xE9},
	{0x4138,0xE6},
	{0x413A,0xF1},
	{0x414C,0x6B},
	{0x414E,0x76},
	{0x4150,0xD8},
	{0x4152,0xE3},
	{0x417E,0x03},
	{0x417F,0x01},
	{0x4186,0xE0},
	{0x4190,0xF3},
	{0x4192,0xF7},
	{0x419C,0x78},
	{0x419E,0x7C},
	{0x41A0,0xE5},
	{0x41A2,0xE9},
	{0x41C8,0xE2},
	{0x41CA,0xFD},
	{0x41DC,0x67},
	{0x41DE,0x82},
	{0x41E0,0xD4},
	{0x41E2,0xEF},
	{0x4200,0xDE},
	{0x4202,0xDA},
	{0x4218,0x63},
	{0x421A,0x5F},
	{0x421C,0xD0},
	{0x421E,0xCC},
	{0x425A,0x82},
	{0x425C,0xEF},
	{0x4348,0xFE},
	{0x4349,0x06},
	{0x4352,0xCE},
	{0x4420,0x0B},
	{0x4421,0x02},
	{0x4422,0x4D},
	{0x4426,0xF5},
	{0x442A,0xE7},
	{0x4432,0xF5},
	{0x4436,0xE7},
	{0x4466,0xB4},
	{0x446E,0x32},
	{0x449F,0x1C},
	{0x44A4,0x2C},
	{0x44A6,0x2C},
	{0x44A8,0x2C},
	{0x44AA,0x2C},
	{0x44B4,0x2C},
	{0x44B6,0x2C},
	{0x44B8,0x2C},
	{0x44BA,0x2C},
	{0x44C4,0x2C},
	{0x44C6,0x2C},
	{0x44C8,0x2C},
	{0x4506,0xF3},
	{0x450E,0xE5},
	{0x4516,0xF3},
	{0x4522,0xE5},
	{0x4524,0xF3},
	{0x452C,0xE5},
	{0x453C,0x22},
	{0x453D,0x1B},
	{0x453E,0x1B},
	{0x453F,0x15},
	{0x4540,0x15},
	{0x4541,0x15},
	{0x4542,0x15},
	{0x4543,0x15},
	{0x4544,0x15},
	{0x4548,0x00},
	{0x4549,0x01},
	{0x454A,0x01},
	{0x454B,0x06},
	{0x454C,0x06},
	{0x454D,0x06},
	{0x454E,0x06},
	{0x454F,0x06},
	{0x4550,0x06},
	{0x4554,0x55},
	{0x4555,0x02},
	{0x4556,0x42},
	{0x4557,0x05},
	{0x4558,0xFD},
	{0x4559,0x05},
	{0x455A,0x94},
	{0x455B,0x06},
	{0x455D,0x06},
	{0x455E,0x49},
	{0x455F,0x07},
	{0x4560,0x7F},
	{0x4561,0x07},
	{0x4562,0xA5},
	{0x4564,0x55},
	{0x4565,0x02},
	{0x4566,0x42},
	{0x4567,0x05},
	{0x4568,0xFD},
	{0x4569,0x05},
	{0x456A,0x94},
	{0x456B,0x06},
	{0x456D,0x06},
	{0x456E,0x49},
	{0x456F,0x07},
	{0x4572,0xA5},
	{0x460C,0x7D},
	{0x460E,0xB1},
	{0x4614,0xA8},
	{0x4616,0xB2},
	{0x461C,0x7E},
	{0x461E,0xA7},
	{0x4624,0xA8},
	{0x4626,0xB2},
	{0x462C,0x7E},
	{0x462E,0x8A},
	{0x4630,0x94},
	{0x4632,0xA7},
	{0x4634,0xFB},
	{0x4636,0x2F},
	{0x4638,0x81},
	{0x4639,0x01},
	{0x463A,0xB5},
	{0x463B,0x01},
	{0x463C,0x26},
	{0x463E,0x30},
	{0x4640,0xAC},
	{0x4641,0x01},
	{0x4642,0xB6},
	{0x4643,0x01},
	{0x4644,0xFC},
	{0x4646,0x25},
	{0x4648,0x82},
	{0x4649,0x01},
	{0x464A,0xAB},
	{0x464B,0x01},
	{0x464C,0x26},
	{0x464E,0x30},
	{0x4654,0xFC},
	{0x4656,0x08},
	{0x4658,0x12},
	{0x465A,0x25},
	{0x4662,0xFC},
	{0x46A2,0xFB},
	{0x46D6,0xF3},
	{0x46E6,0x00},
	{0x46E8,0xFF},
	{0x46E9,0x03},
	{0x46EC,0x7A},
	{0x46EE,0xE5},
	{0x46F4,0xEE},
	{0x46F6,0xF2},
	{0x470C,0xFF},
	{0x470D,0x03},
	{0x470E,0x00},
	{0x4714,0xE0},
	{0x4716,0xE4},
	{0x471E,0xED},
	{0x472E,0x00},
	{0x4730,0xFF},
	{0x4731,0x03},
	{0x4734,0x7B},
	{0x4736,0xDF},
	{0x4754,0x7D},
	{0x4756,0x8B},
	{0x4758,0x93},
	{0x475A,0xB1},
	{0x475C,0xFB},
	{0x475E,0x09},
	{0x4760,0x11},
	{0x4762,0x2F},
	{0x4766,0xCC},
	{0x4776,0xCB},
	{0x477E,0x4A},
	{0x478E,0x49},
	{0x4794,0x7C},
	{0x4796,0x8F},
	{0x4798,0xB3},
	{0x4799,0x00},
	{0x479A,0xCC},
	{0x479C,0xC1},
	{0x479E,0xCB},
	{0x47A4,0x7D},
	{0x47A6,0x8E},
	{0x47A8,0xB4},
	{0x47A9,0x00},
	{0x47AA,0xC0},
	{0x47AC,0xFA},
	{0x47AE,0x0D},
	{0x47B0,0x31},
	{0x47B1,0x01},
	{0x47B2,0x4A},
	{0x47B3,0x01},
	{0x47B4,0x3F},
	{0x47B6,0x49},
	{0x47BC,0xFB},
	{0x47BE,0x0C},
	{0x47C0,0x32},
	{0x47C1,0x01},
	{0x47C2,0x3E},
	{0x47C3,0x01},
	{0x4E3C,0x07},
	{IMX678_REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx678_interal_sync_master_start_regs[] = {
	{0x3010, 0x07},
	{0x31a1, 0x00},
	{IMX678_REG_NULL, 0x00},
};
static __maybe_unused const struct regval imx678_interal_sync_master_stop_regs[] = {
	{0x31a1, 0x0f},
	{IMX678_REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx678_external_sync_master_start_regs[] = {
	{0x3010, 0x05},
	{0x31a1, 0x03},
	{0x31d9, 0x01},
	{IMX678_REG_NULL, 0x00},
};
static __maybe_unused const struct regval imx678_external_sync_master_stop_regs[] = {
	{0x31a1, 0x0f},
	{IMX678_REG_NULL, 0x00},
};

static __maybe_unused const struct regval imx678_slave_start_regs[] = {
	{0x3010, 0x05},
	{0x31a1, 0x0f},
	{IMX678_REG_NULL, 0x00},
};

static const struct imx678_mode supported_modes[] = {
	{
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x044C * 4,
		.vts_def = 0x08CA,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.global_reg_list = imx678_10_3840x2160_global_regs,
		.hdr_mode = NO_HDR,
		.vclk_freq = IMX678_XVCLK_FREQ_37,
		.bpp = 10,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, 
};

static const s64 link_freq_menu_items[] = {
	IMX678_LINK_FREQ_445,
	IMX678_LINK_FREQ_445,
	IMX678_LINK_FREQ_445
};

static const char * const imx678_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int imx678_write_reg(struct i2c_client *client, u16 reg,
			    int len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int imx678_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != IMX678_REG_NULL; i++)
		if (unlikely(regs[i].addr == IMX678_REG_DELAY))
			usleep_range(regs[i].val, regs[i].val * 2);
		else
			ret = imx678_write_reg(client, regs[i].addr,
					       IMX678_REG_VALUE_08BIT,
					       regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int imx678_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			   u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret, i;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	for (i = 0; i < 3; i++) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs))
			break;
	}
	if (ret != ARRAY_SIZE(msgs) && i == 3)
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int imx678_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx678 *imx678 = to_imx678(sd);
	const struct imx678_mode *mode;
	s64 h_blank, vblank_def;
	s64 dst_pixel_rate = 0;
	int ret = 0;

	mutex_lock(&imx678->mutex);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx678->mutex);
		return -ENOTTY;
#endif
	} else {
		imx678->cur_mode = mode;
		imx678->cur_vts = imx678->cur_mode->vts_def;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx678->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx678->vblank, vblank_def,
					 IMX678_VTS_MAX - mode->height,
					 1, vblank_def);
		if (imx678->cur_vclk_freq != mode->vclk_freq) {
			clk_disable_unprepare(imx678->xvclk);
			ret = clk_set_rate(imx678->xvclk, mode->vclk_freq);
			ret |= clk_prepare_enable(imx678->xvclk);
			if (ret < 0) {
				dev_err(&imx678->client->dev, "Failed to enable xvclk\n");
				mutex_unlock(&imx678->mutex);
				return ret;
			}
			imx678->cur_vclk_freq = mode->vclk_freq;
		}
		if (imx678->cur_mipi_freq_idx != mode->mipi_freq_idx) {
			dst_pixel_rate = ((u32)link_freq_menu_items[mode->mipi_freq_idx]) /
				mode->bpp * 2 * IMX678_LANES;
			__v4l2_ctrl_s_ctrl_int64(imx678->pixel_rate,
						 dst_pixel_rate);
			__v4l2_ctrl_s_ctrl(imx678->link_freq,
					   mode->mipi_freq_idx);
			imx678->cur_mipi_freq_idx = mode->mipi_freq_idx;
		}
	}
	mutex_unlock(&imx678->mutex);
	return 0;
}

static int imx678_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx678 *imx678 = to_imx678(sd);
	const struct imx678_mode *mode = imx678->cur_mode;

	mutex_lock(&imx678->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&imx678->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&imx678->mutex);

	return 0;
}

static int imx678_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx678 *imx678 = to_imx678(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = imx678->cur_mode->bus_fmt;

	return 0;
}

static int imx678_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int imx678_enable_test_pattern(struct imx678 *imx678, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | IMX678_TEST_PATTERN_ENABLE;
	else
		val = IMX678_TEST_PATTERN_DISABLE;

	return imx678_write_reg(imx678->client,
				IMX678_REG_TEST_PATTERN,
				IMX678_REG_VALUE_08BIT,
				val);
}

static int imx678_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx678 *imx678 = to_imx678(sd);
	const struct imx678_mode *mode = imx678->cur_mode;

	mutex_lock(&imx678->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&imx678->mutex);

	return 0;
}

static int imx678_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct imx678 *imx678 = to_imx678(sd);
	const struct imx678_mode *mode = imx678->cur_mode;
	u32 val = 0;

	val = 1 << (IMX678_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->flags = (mode->hdr_mode == NO_HDR) ? val : (val | V4L2_MBUS_CSI2_CHANNEL_1);
	config->type = V4L2_MBUS_CSI2_DPHY;
	return 0;
}

static void imx678_get_module_inf(struct imx678 *imx678,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, IMX678_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, imx678->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, imx678->len_name, sizeof(inf->base.lens));
}

static long imx678_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx678 *imx678 = to_imx678(sd);
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 stream = 0;
    u32 *sync_mode = NULL;
	
	
	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		imx678_get_module_inf(imx678, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = imx678->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = imx678_write_reg(imx678->client, IMX678_REG_CTRL_MODE,
				IMX678_REG_VALUE_08BIT, 0);
		else
			ret = imx678_write_reg(imx678->client, IMX678_REG_CTRL_MODE,
				IMX678_REG_VALUE_08BIT, 1);
		break;
	case RKMODULE_GET_SYNC_MODE:
		sync_mode = (u32 *)arg;
		*sync_mode = imx678->sync_mode;
		v4l2_err(&imx678->subdev, "get sync mode %d\n",*sync_mode);
		break;
	case RKMODULE_SET_SYNC_MODE:
		sync_mode = (u32 *)arg;
		imx678->sync_mode = *sync_mode;	
		v4l2_err(&imx678->subdev, "set sync mode %d\n",*sync_mode);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx678_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32 stream = 0;
	u32 sync_mode;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx678_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = imx678_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = imx678_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_SYNC_MODE:
		ret = imx678_ioctl(sd, cmd, &sync_mode);
		if (!ret) {
			ret = copy_to_user(up, &sync_mode, sizeof(u32));
			if (ret)
				ret = -EFAULT;
		}
		break;
	case RKMODULE_SET_SYNC_MODE:
		ret = copy_from_user(&sync_mode, up, sizeof(u32));
		if (!ret)
			ret = imx678_ioctl(sd, cmd, &sync_mode);
		else
			ret = -EFAULT;	
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __imx678_start_stream(struct imx678 *imx678)
{
	int ret;

	ret = imx678_write_array(imx678->client, imx678->cur_mode->global_reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&imx678->mutex);
	ret = v4l2_ctrl_handler_setup(&imx678->ctrl_handler);
	mutex_lock(&imx678->mutex);
	if (ret)
	    return ret;
	
	usleep_range(24000, 30000);
	
	
	if (imx678->sync_mode == EXTERNAL_MASTER_MODE) {
		ret |= imx678_write_array(imx678->client, imx678_external_sync_master_start_regs);
		v4l2_err(&imx678->subdev, "cur externam master mode\n");
	} else if (imx678->sync_mode == INTERNAL_MASTER_MODE) {
		ret |= imx678_write_array(imx678->client, imx678_interal_sync_master_start_regs);
		v4l2_err(&imx678->subdev, "cur intertal master\n");
	} else if (imx678->sync_mode == SLAVE_MODE) {
		ret |= imx678_write_array(imx678->client, imx678_slave_start_regs);
		v4l2_err(&imx678->subdev, "cur slave mode\n");
	}
	if (imx678->sync_mode == NO_SYNC_MODE) {
		v4l2_err(&imx678->subdev, "cur NO SYNC mode\n");
	    ret = imx678_write_reg(imx678->client, IMX678_REG_CTRL_MODE,
				IMX678_REG_VALUE_08BIT, 0);		
	    usleep_range(24000, 30000);
	    printk("------- standby Sony IMX678 Sensor 4K@30 10bit Initial ret = %d-------\n",ret);
        ret = imx678_write_reg(imx678->client, imx678_REG_MARSTER_MODE,
				IMX678_REG_VALUE_08BIT, 0);		
	    printk("------- master Sony IMX678 Sensor 4K@30 10bit Initial ret = %d-------\n",ret);
	}
     else {
		ret |= imx678_write_reg(imx678->client, imx678_REG_MARSTER_MODE,
					IMX678_REG_VALUE_08BIT, 0);
	}	
    
//	imx678_write_reg(imx678->client, IMX678_HREVERSE_REG,IMX678_REG_VALUE_08BIT, 0x01);
//	imx678_write_reg(imx678->client, IMX678_VREVERSE_REG,IMX678_REG_VALUE_08BIT, 0x01);
//	usleep_range(24000, 30000);
	return ret;
}

static int __imx678_stop_stream(struct imx678 *imx678)
{
	int ret = 0;
	
	ret = imx678_write_reg(imx678->client, IMX678_REG_CTRL_MODE,
				IMX678_REG_VALUE_08BIT, 1);
		if (imx678->sync_mode == EXTERNAL_MASTER_MODE)
		ret |= imx678_write_array(imx678->client, imx678_external_sync_master_stop_regs);
	else if (imx678->sync_mode == INTERNAL_MASTER_MODE)
		ret |= imx678_write_array(imx678->client, imx678_interal_sync_master_stop_regs);
	
	return ret;
}

static int imx678_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx678 *imx678 = to_imx678(sd);
	struct i2c_client *client = imx678->client;
	int ret = 0;
	
	dev_info(&client->dev, "%s on:%d\n", __func__,on);
	
	mutex_lock(&imx678->mutex);
	on = !!on;
	if (on == imx678->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx678_start_stream(imx678);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx678_stop_stream(imx678);
		pm_runtime_put(&client->dev);
	}

	imx678->streaming = on;

unlock_and_return:
	mutex_unlock(&imx678->mutex);

	return ret;
}

static int imx678_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx678 *imx678 = to_imx678(sd);
	struct i2c_client *client = imx678->client;
	int ret = 0;

	mutex_lock(&imx678->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx678->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		imx678->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx678->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx678->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 imx678_cal_delay(u32 cycles, struct imx678 *imx678)
{
	return DIV_ROUND_UP(cycles, IMX678_XVCLK_FREQ_37 / 1000 / 1000);
}

static int __imx678_power_on(struct imx678 *imx678)
{
	int ret;
	u32 delay_us;
	s64 vclk_freq;
	struct device *dev = &imx678->client->dev;

	if (!IS_ERR_OR_NULL(imx678->pins_default)) {
		ret = pinctrl_select_state(imx678->pinctrl,
					   imx678->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	vclk_freq = IMX678_XVCLK_FREQ_37;

	ret = clk_set_rate(imx678->xvclk, vclk_freq);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(imx678->xvclk) != vclk_freq)
		dev_warn(dev, "xvclk mismatched, modes are based on 37.125MHz\n");
	ret = clk_prepare_enable(imx678->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(imx678->reset_gpio))
		gpiod_set_value_cansleep(imx678->reset_gpio, 0);

	ret = regulator_bulk_enable(IMX678_NUM_SUPPLIES, imx678->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(imx678->reset_gpio))
		gpiod_set_value_cansleep(imx678->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(imx678->pwdn_gpio))
		gpiod_set_value_cansleep(imx678->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = imx678_cal_delay(8192, imx678);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(imx678->xvclk);

	return ret;
}

static void __imx678_power_off(struct imx678 *imx678)
{
	//return;
	if (!IS_ERR(imx678->pwdn_gpio))
		gpiod_set_value_cansleep(imx678->pwdn_gpio, 0);
	clk_disable_unprepare(imx678->xvclk);
	if (!IS_ERR(imx678->reset_gpio))
		gpiod_set_value_cansleep(imx678->reset_gpio, 0);
	regulator_bulk_disable(IMX678_NUM_SUPPLIES, imx678->supplies);
}

static int imx678_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	return __imx678_power_on(imx678);
}

static int imx678_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	__imx678_power_off(imx678);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx678_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx678 *imx678 = to_imx678(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx678_mode *def_mode = &supported_modes[0];

	mutex_lock(&imx678->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx678->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx678_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH 3840
#define DST_HEIGHT 2160

static int imx678_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct imx678 *imx678 = to_imx678(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = CROP_START(imx678->cur_mode->width, DST_WIDTH);
		sel->r.width = DST_WIDTH;
		sel->r.top = CROP_START(imx678->cur_mode->height, DST_HEIGHT);
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops imx678_pm_ops = {
	SET_RUNTIME_PM_OPS(imx678_runtime_suspend,
			   imx678_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx678_internal_ops = {
	.open = imx678_open,
};
#endif

static const struct v4l2_subdev_core_ops imx678_core_ops = {
	.s_power = imx678_s_power,
	.ioctl = imx678_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx678_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx678_video_ops = {
	.s_stream = imx678_s_stream,
	.g_frame_interval = imx678_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx678_pad_ops = {
	.enum_mbus_code = imx678_enum_mbus_code,
	.enum_frame_size = imx678_enum_frame_sizes,
	.enum_frame_interval = imx678_enum_frame_interval,
	.get_fmt = imx678_get_fmt,
	.set_fmt = imx678_set_fmt,
	.get_selection = imx678_get_selection,
	.get_mbus_config = imx678_g_mbus_config,
};

static const struct v4l2_subdev_ops imx678_subdev_ops = {
	.core	= &imx678_core_ops,
	.video	= &imx678_video_ops,
	.pad	= &imx678_pad_ops,
};

static int imx678_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx678 *imx678 = container_of(ctrl->handler,
					     struct imx678, ctrl_handler);
	struct i2c_client *client = imx678->client;
	s64 max;
	int ret = 0;
	u32 shr0 = 0;
	u32 vts = 0;
#if 0	
	u32 flip = 0;
#endif
	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = imx678->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(imx678->exposure,
					 imx678->exposure->minimum, max,
					 imx678->exposure->step,
					 imx678->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		shr0 = imx678->cur_vts - ctrl->val;
		/* 4 least significant bits of expsoure are fractional part */
		ret = imx678_write_reg(imx678->client,
				       IMX678_SHR_EXPO_REG_H,
				       IMX678_REG_VALUE_08BIT,
				       IMX678_FETCH_EXP_H(shr0));
		ret |= imx678_write_reg(imx678->client,
					IMX678_SHR_EXPO_REG_M,
					IMX678_REG_VALUE_08BIT,
					IMX678_FETCH_EXP_M(shr0));
		ret |= imx678_write_reg(imx678->client,
					IMX678_SHR_EXPO_REG_L,
					IMX678_REG_VALUE_08BIT,
					IMX678_FETCH_EXP_L(shr0));
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = imx678_write_reg(imx678->client,
				       IMX678_REG_GAIN,
				       IMX678_REG_VALUE_08BIT, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + imx678->cur_mode->height;
		/*
		 * vts of hdr mode is double to correct T-line calculation.
		 * Restore before write to reg.
		 */
		if (imx678->cur_mode->hdr_mode == HDR_X2) {
			vts = ((vts + 3) >> 2) * 4;
			imx678->cur_vts = vts;
			vts = vts >> 1;
		} else {
			imx678->cur_vts = vts;
		}
		ret = imx678_write_reg(imx678->client,
				       IMX678_REG_VTS_H,
				       IMX678_REG_VALUE_08BIT,
				       IMX678_FETCH_VTS_H(vts));
		ret |= imx678_write_reg(imx678->client,
					IMX678_REG_VTS_M,
					IMX678_REG_VALUE_08BIT,
					IMX678_FETCH_VTS_M(vts));
		ret |= imx678_write_reg(imx678->client,
					IMX678_REG_VTS_L,
					IMX678_REG_VALUE_08BIT,
					IMX678_FETCH_VTS_L(vts));
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx678_enable_test_pattern(imx678, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
#if 0	
		ret = imx678_write_reg(imx678->client, IMX678_HREVERSE_REG,
				       IMX678_REG_VALUE_08BIT, !!ctrl->val);
#endif					   
		break;
	case V4L2_CID_VFLIP:
#if 0	
		flip = ctrl->val;
		if (flip) {
			ret = imx678_write_reg(imx678->client, IMX678_VREVERSE_REG,
				IMX678_REG_VALUE_08BIT, !!flip);
		} else {
			ret = imx678_write_reg(imx678->client, IMX678_VREVERSE_REG,
				IMX678_REG_VALUE_08BIT, !!flip);
				}
#endif		
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx678_ctrl_ops = {
	.s_ctrl = imx678_set_ctrl,
};

static int imx678_initialize_controls(struct imx678 *imx678)
{
	const struct imx678_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	s64 dst_pixel_rate = 0;

	handler = &imx678->ctrl_handler;
	mode = imx678->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &imx678->mutex;

	imx678->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
						   V4L2_CID_LINK_FREQ,
						   2, 0, link_freq_menu_items);

	dst_pixel_rate = ((u32)link_freq_menu_items[mode->mipi_freq_idx]) /
		mode->bpp * 2 * IMX678_LANES;

	imx678->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
					       V4L2_CID_PIXEL_RATE,
					       0, PIXEL_RATE_WITH_445M_10BIT,
					       1, dst_pixel_rate);
	v4l2_ctrl_s_ctrl(imx678->link_freq,
			 mode->mipi_freq_idx);
	imx678->cur_mipi_freq_idx = mode->mipi_freq_idx;
	imx678->cur_vclk_freq = mode->vclk_freq;

	h_blank = mode->hts_def - mode->width;
	imx678->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (imx678->hblank)
		imx678->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx678->vblank = v4l2_ctrl_new_std(handler, &imx678_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   IMX678_VTS_MAX - mode->height,
					   1, vblank_def);
	imx678->cur_vts = mode->vts_def;
	exposure_max = mode->vts_def - 4;
	imx678->exposure = v4l2_ctrl_new_std(handler, &imx678_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX678_EXPOSURE_MIN,
					     exposure_max,
					     IMX678_EXPOSURE_STEP,
					     mode->exp_def);

	imx678->anal_gain = v4l2_ctrl_new_std(handler, &imx678_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN,
					      IMX678_GAIN_MIN,
					      IMX678_GAIN_MAX,
					      IMX678_GAIN_STEP,
					      IMX678_GAIN_DEFAULT);

	imx678->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &imx678_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(imx678_test_pattern_menu) - 1,
				0, 0, imx678_test_pattern_menu);

	v4l2_ctrl_new_std(handler, &imx678_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &imx678_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx678->client->dev,
			"Failed to init controls(  %d  )\n", ret);
		goto err_free_handler;
	}

	imx678->subdev.ctrl_handler = handler;
	imx678->has_init_exp = false;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx678_check_sensor_id(struct imx678 *imx678,
				  struct i2c_client *client)
{
	struct device *dev = &imx678->client->dev;
	u32 id = 0;
	int ret, i;

	for (i = 0; i < 10; i++) {
		ret = imx678_read_reg(client, IMX678_REG_CHIP_ID,
				      IMX678_REG_VALUE_16BIT, &id);
		if (id == IMX678_CHIP_ID)
			break;
	}

	if (id != IMX678_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		usleep_range(2000, 4000);
		return -ENODEV;
	}

	dev_info(dev, "Detected imx678 id:%06x\n", IMX678_CHIP_ID);

	return 0;
}

static int imx678_configure_regulators(struct imx678 *imx678)
{
	unsigned int i;

	for (i = 0; i < IMX678_NUM_SUPPLIES; i++)
		imx678->supplies[i].supply = imx678_supply_names[i];

	return devm_regulator_bulk_get(&imx678->client->dev,
				       IMX678_NUM_SUPPLIES,
				       imx678->supplies);
}


static int imx678_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx678 *imx678;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	
	const char *sync_mode_name = NULL;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 IMX678_DRIVER_VERSION >> 16,
		 (IMX678_DRIVER_VERSION & 0xff00) >> 8,
		 IMX678_DRIVER_VERSION & 0x00ff);

	imx678 = devm_kzalloc(dev, sizeof(*imx678), GFP_KERNEL);
	if (!imx678)
		return -ENOMEM;


	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx678->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx678->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx678->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx678->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

    ret = of_property_read_string(node, RKMODULE_CAMERA_SYNC_MODE,
				      &sync_mode_name);
	if (ret) {
		imx678->sync_mode = NO_SYNC_MODE;
		dev_err(dev, "could not get sync mode!\n");
	} else {
		if (strcmp(sync_mode_name, RKMODULE_EXTERNAL_MASTER_MODE) == 0)
			imx678->sync_mode = EXTERNAL_MASTER_MODE;
		else if (strcmp(sync_mode_name, RKMODULE_INTERNAL_MASTER_MODE) == 0)
			imx678->sync_mode = INTERNAL_MASTER_MODE;
		else if (strcmp(sync_mode_name, RKMODULE_SLAVE_MODE) == 0)
			imx678->sync_mode = SLAVE_MODE;
	}
	imx678->client = client;
	
	imx678->cur_mode = &supported_modes[0];

	imx678->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx678->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx678->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx678->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx678->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx678->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	imx678->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(imx678->pinctrl)) {
		imx678->pins_default =
			pinctrl_lookup_state(imx678->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(imx678->pins_default))
			dev_info(dev, "could not get default pinstate\n");

		imx678->pins_sleep =
			pinctrl_lookup_state(imx678->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(imx678->pins_sleep))
			dev_info(dev, "could not get sleep pinstate\n");
	} else {
		dev_info(dev, "no pinctrl\n");
	}

	ret = imx678_configure_regulators(imx678);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&imx678->mutex);

	sd = &imx678->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx678_subdev_ops);

	ret = imx678_initialize_controls(imx678);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx678_power_on(imx678);
	if (ret)
		goto err_free_handler;

	ret = imx678_check_sensor_id(imx678, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx678_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx678->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx678->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx678->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx678->module_index, facing,
		 IMX678_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__imx678_power_off(imx678);
err_free_handler:
	v4l2_ctrl_handler_free(&imx678->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx678->mutex);

	return ret;
}

static int imx678_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx678 *imx678 = to_imx678(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx678->ctrl_handler);
	mutex_destroy(&imx678->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx678_power_off(imx678);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#ifndef INNOSZ_WEEWA_DRIVER

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx678_of_match[] = {
	{ .compatible = "sony,imx678" },
	{},
};
MODULE_DEVICE_TABLE(of, imx678_of_match);
#endif

static const struct i2c_device_id imx678_match_id[] = {
	{ "sony,imx678", 0 },
	{ },
};

static struct i2c_driver imx678_i2c_driver = {
	.driver = {
		.name = IMX678_NAME,
		.pm = &imx678_pm_ops,
		.of_match_table = of_match_ptr(imx678_of_match),
	},
	.probe		= &imx678_probe,
	.remove		= &imx678_remove,
	.id_table	= imx678_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx678_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx678_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx678 sensor driver");
MODULE_LICENSE("GPL v2");
#else
#include "imx334.c"
#define WEEWA_NAME "weewacam"
int sensor_type = 0;
static int weewa_check_sensor_id(struct imx678 *imx678,
				  struct i2c_client *client)
{
	struct device *dev = &imx678->client->dev;
	u32 id = 0;
	int ret, i;

	for (i = 0; i < 10; i++) {
		ret = imx678_read_reg(client, IMX678_REG_CHIP_ID,
				      IMX678_REG_VALUE_16BIT, &id);
		if (id == IMX678_CHIP_ID){
			sensor_type = 0x678;
		    break;
		}
		else if (id == IMX334_CHIP_ID){
			sensor_type = 0x334;
			break;
		}
	}

	if (id == IMX678_CHIP_ID||id == IMX334_CHIP_ID) {
        dev_info(dev, "Detected camera id:%06x\n", id);
	    return 0;
	}
	dev_err(dev, "weewacam Unexpected sensor id(%06x), ret(%d)\n", id, ret);
	usleep_range(2000, 4000);
	return -ENODEV;
}

static int weewa_probe(struct i2c_client *client,
			const struct i2c_device_id *id){
	struct device *dev = &client->dev;
	struct imx678 * imx678;
    int ret;

	imx678 = devm_kzalloc(dev, sizeof(*imx678), GFP_KERNEL);
	if (!imx678)
		return -ENOMEM;
	imx678->client = client;

	imx678->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx678->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx678->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx678->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx678->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx678->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	imx678->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(imx678->pinctrl)) {
		imx678->pins_default =
			pinctrl_lookup_state(imx678->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(imx678->pins_default))
			dev_info(dev, "could not get default pinstate\n");

	} else {
		dev_info(dev, "no pinctrl\n");
	}

	ret = imx678_configure_regulators(imx678);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}
	ret = __imx678_power_on(imx678);
	if (ret){
		dev_err(dev, "__imx678_power_on failed\n");
		goto err_power_off;
	}

	ret = weewa_check_sensor_id(imx678, client);
	if (ret){
        dev_err(dev, "weewa_check_sensor_id failed\n");
		goto err_power_off;
	}
    devm_gpiod_put(dev, imx678->reset_gpio );
	devm_gpiod_put(dev, imx678->pwdn_gpio );
	//devm_pinctrl_put(imx678->pinctrl);
    devm_kfree(dev,imx678);
    imx678 = NULL;
	dev_info(dev,"sensor_type=0x%x",sensor_type);
    if (sensor_type==0x678)
	    return imx678_probe(client,id);
    else if (sensor_type==0x334)
        return imx334_probe(client,id);
    else 
        return -ENODEV;

err_power_off:
	__imx678_power_off(imx678);	
    devm_kfree(dev,imx678);
    imx678 = NULL;
    return ret;

}

static int weewa_remove(struct i2c_client *client){
	printk("weewa_remove sensor_type=%d\n",sensor_type);
    if (sensor_type==0x678)
	    return imx678_remove(client);
    else if (sensor_type==0x334)
        return imx334_remove(client);
	else
		return 0;
}

static int weewa_runtime_suspend(struct device *dev){
	printk("weewa_runtime_suspend sensor_type=%d\n",sensor_type);
    if (sensor_type==0x678)
	    return imx678_runtime_suspend(dev);
    else if (sensor_type==0x334)
        return imx334_runtime_suspend(dev);
	else
		return 0;
}

static int weewa_runtime_resume(struct device *dev){
	printk("weewa_runtime_resume sensor_type=%d\n",sensor_type);
    if (sensor_type==0x678)
	    return imx678_runtime_resume(dev);
    else if (sensor_type==0x334)
        return imx334_runtime_resume(dev);
	else
		return 0;
}

static const struct dev_pm_ops weewa_pm_ops = {
	SET_RUNTIME_PM_OPS(weewa_runtime_suspend,
			   weewa_runtime_resume, NULL)
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id weewa_of_match[] = {
	{ .compatible = "innosz,weewa" },
	{},
};
MODULE_DEVICE_TABLE(of, weewa_of_match);
#endif

static const struct i2c_device_id weewa_match_id[] = {
	{ "innosz,weewa", 0 },
	{ },
};

static struct i2c_driver weewa_i2c_driver = {
	.driver = {
		.name = WEEWA_NAME,
		.pm = &weewa_pm_ops,
		.of_match_table = of_match_ptr(weewa_of_match),
	},
	.probe		= &weewa_probe,
	.remove		= &weewa_remove,
	.id_table	= weewa_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&weewa_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&weewa_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("innosz weewa sensor driver");
MODULE_LICENSE("GPL v2");
#endif
