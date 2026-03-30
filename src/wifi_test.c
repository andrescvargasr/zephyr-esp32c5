/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_credentials.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static int wifi_autoconnect_from_nvs(void)
{
	struct net_if *iface = net_if_get_default();
	int ret;

	if (iface == NULL) {
		printf("No default network interface\n");
		return -ENODEV;
	}

	if (wifi_credentials_is_empty()) {
		printf("No Wi-Fi credentials saved in NVS\n");
		return -ENOENT;
	}

#ifdef CONFIG_WIFI_CREDENTIALS_CONNECT_STORED
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
	if (ret) {
		printf("Auto-connect request failed: %d\n", ret);
		return ret;
	}

	printf("Auto-connect requested using saved Wi-Fi credentials\n");
	return 0;
#else
	printf("CONFIG_WIFI_CREDENTIALS_CONNECT_STORED is not enabled\n");
	return -ENOTSUP;
#endif
}

int main(void)
{
	int ret;
	bool led_state = true;

	ret = wifi_autoconnect_from_nvs();
	if (ret < 0) {
		printf("Wi-Fi autoconnect skipped (%d)\n", ret);
	}

	if (!gpio_is_ready_dt(&led))
	{
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0)
	{
		return 0;
	}

	while (1)
	{
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0)
		{
			return 0;
		}

		led_state = !led_state;
		// printf("LED state: %s\n", led_state ? "ON" : "OFF");
		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
