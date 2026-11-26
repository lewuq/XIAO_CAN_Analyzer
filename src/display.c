/* SH8501G initialization uses the proven bytewise command transport. */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include "sh8501_init.h"
#include "display.h"

#ifndef SCREEN_SPI_HZ
#define SCREEN_SPI_HZ 4000000U
#endif
#ifndef SCREEN_BRIGHTNESS_PERCENT
#define SCREEN_BRIGHTNESS_PERCENT 50U
#endif
#ifndef SCREEN_BURST_PIXELS
#define SCREEN_BURST_PIXELS 1
#endif

#define CTRL_NODE DT_PATH(zephyr_user)

BUILD_ASSERT(SCREEN_BRIGHTNESS_PERCENT <= 100U, "Brightness must be 0..100");

static const struct device *const spi = DEVICE_DT_GET(DT_NODELABEL(spi3));
static const struct gpio_dt_spec cs = GPIO_DT_SPEC_GET(CTRL_NODE, cs_gpios);
static const struct gpio_dt_spec dc = GPIO_DT_SPEC_GET(CTRL_NODE, dc_gpios);
static const struct gpio_dt_spec rst = GPIO_DT_SPEC_GET(CTRL_NODE, reset_gpios);
static const struct spi_config spi_cfg = {
	.frequency = SCREEN_SPI_HZ,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	.slave = 0,
	/* CS is handled by the display driver. */
};

static int write_bytes(const void *bytes, size_t len)
{
	struct spi_buf buf = { .buf = (void *)bytes, .len = len };
	const struct spi_buf_set tx = { .buffers = &buf, .count = 1 };

	return spi_write(spi, &spi_cfg, &tx);
}

static int send_bytes(int data_mode, const void *data, size_t len)
{
	const uint8_t *bytes = data;
	int ret = gpio_pin_set_dt(&dc, data_mode);

	if (ret < 0) {
		return ret;
	}
	while (len != 0U) {
		size_t count = 1U;

		ret = gpio_pin_set_dt(&cs, 1); /* select active-low CS */
		if (ret < 0) {
			return ret;
		}
		ret = write_bytes(bytes, count);
		int deselect_ret = gpio_pin_set_dt(&cs, 0);

		if (ret < 0 || deselect_ret < 0) {
			return ret < 0 ? ret : deselect_ret;
		}
		bytes += count;
		len -= count;
	}
	return 0;
}

static int command(uint8_t cmd, const void *data, size_t len)
{
	int ret = send_bytes(0, &cmd, 1);

	return ret == 0 && len != 0U ? send_bytes(1, data, len) : ret;
}

static int command_u8(uint8_t cmd, uint8_t data)
{
	return command(cmd, &data, 1);
}

static int brightness(unsigned int percent)
{
	/* 8-bit DCS WRDISBV (0x51): round 50% to 128/255, not pixel dimming. */
	return command_u8(0x51, (uint8_t)((percent * 255U + 50U) / 100U));
}

static int begin_window(const struct panel_rect *rect)
{
	const uint8_t columns[] = { 0, rect->x0, 0, rect->x1 };
	const uint8_t rows[] = { 0, rect->y0, 0, rect->y1 };
	int ret = command(0x2a, columns, sizeof(columns));

	if (ret == 0) {
		ret = command(0x2b, rows, sizeof(rows));
	}
	if (ret == 0) {
		ret = command(0x2c, NULL, 0);
	}
	if (ret == 0) {
		/* Match LCD_Address_Set(): RAMWR, 5 ms, RAMWR, then pixel bytes. */
		k_msleep(5);
		ret = command(0x2c, NULL, 0);
	}
	return ret;
}

int display_init(void)
{
	if (!device_is_ready(spi) || !gpio_is_ready_dt(&cs) ||
	    !gpio_is_ready_dt(&dc) || !gpio_is_ready_dt(&rst)) {
		return -ENODEV;
	}
	int ret = gpio_pin_configure_dt(&cs, GPIO_OUTPUT_INACTIVE);

	if (ret == 0) {
		ret = gpio_pin_configure_dt(&dc, GPIO_OUTPUT_INACTIVE);
	}
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&rst, GPIO_OUTPUT_INACTIVE);
	}
	if (ret < 0) {
		return ret;
	}
	/* Same reset waveform as the supplied LCD_Init(): high/low/high. */
	k_msleep(100);
	ret = gpio_pin_set_dt(&rst, 1);
	if (ret < 0) {
		return ret;
	}
	k_msleep(250);
	ret = gpio_pin_set_dt(&rst, 0);
	if (ret < 0) {
		return ret;
	}
	k_msleep(250);
	for (size_t i = 0; i < ARRAY_SIZE(sh8501_init_commands); ++i) {
		const struct sh8501_init_command *entry = &sh8501_init_commands[i];

		if (entry->command == 0x29) {
			/* Apply the requested brightness before the panel becomes visible. */
			ret = brightness(SCREEN_BRIGHTNESS_PERCENT);
			if (ret < 0) {
				return ret;
			}
		}
		ret = command(entry->command, sh8501_init_data + entry->offset,
			      entry->count);
		if (ret < 0) {
			printk("Init command %u (0x%02x) failed: %d\n",
			       (unsigned int)i, entry->command, ret);
			return ret;
		}
		if (entry->delay_ms != 0U) {
			k_msleep(entry->delay_ms);
		}
	}
	k_msleep(120);
	return brightness(SCREEN_BRIGHTNESS_PERCENT);
}

int display_update(const uint8_t *pixels, const struct panel_rect *rect)
{
	if (!panel_window_valid(rect)) {
		return -EINVAL;
	}
	int ret = begin_window(rect);
	if (ret < 0) {
		return ret;
	}
	size_t row_bytes = (rect->x1 - rect->x0 + 1U) * 2U;
	size_t row_count = rect->y1 - rect->y0 + 1U;
	const uint8_t *source = pixels + rect->y0 * PANEL_STRIDE + rect->x0 * 2U;
#if SCREEN_BURST_PIXELS
	static uint8_t stripe[PANEL_STRIDE * 8U];
	ret = gpio_pin_set_dt(&dc, 1);
	if (ret < 0) { return ret; }
	ret = gpio_pin_set_dt(&cs, 1);
	if (ret < 0) { return ret; }
	if (row_bytes == PANEL_STRIDE) {
		ret = write_bytes(source, row_bytes * row_count);
	} else {
		while (row_count != 0U && ret == 0) {
			size_t rows = MIN(row_count, sizeof(stripe) / row_bytes);
			for (size_t i = 0; i < rows; ++i) {
				memcpy(stripe + i * row_bytes, source + i * PANEL_STRIDE, row_bytes);
			}
			ret = write_bytes(stripe, rows * row_bytes);
			source += rows * PANEL_STRIDE;
			row_count -= rows;
		}
	}
	int deselect = gpio_pin_set_dt(&cs, 0);
	return ret < 0 ? ret : deselect;
#else
	for (size_t i = 0; i < row_count && ret == 0; ++i) {
		ret = send_bytes(1, source + i * PANEL_STRIDE, row_bytes);
	}
	return ret;
#endif
}
