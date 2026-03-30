/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <errno.h>

#include <stdio.h>
#include <zephyr/drivers/gpio.h>

// From MQTT publisher sample
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_mqtt_publisher_sample, LOG_LEVEL_DBG);

#include <zephyr/posix/poll.h>
#include <zephyr/posix/arpa/inet.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/random/random.h>
#if defined(CONFIG_LOG_BACKEND_MQTT)
#include <zephyr/logging/log_backend_mqtt.h>
#endif

#include <string.h>

#include "config.h"
#include "net_sample_common.h"

// Obtain IP
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_credentials.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/* The mqtt client struct */
static struct mqtt_client client_ctx;

static bool connected;

/* Whether to include full topic in the publish message, or alias only (MQTT 5). */
static bool include_topic;
static bool aliases_enabled;

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
// static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static int wifi_autoconnect_from_nvs(void) {
	struct net_if *iface = net_if_get_default();
	int ret;

	if(iface == NULL) {
		LOG_ERR("No default network interface");
		return -ENODEV;
	}

	if(wifi_credentials_is_empty()) {
		LOG_WRN("No Wi-Fi credentials save in NVS");
		return -ENOENT;
	}

	#ifdef CONFIG_WIFI_CREDENTIALS_CONNECT_STORED
		ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
		if (ret)
		{
			LOG_ERR("Auto-connect request failed: %d", ret);
			return ret;
		}

		LOG_INF("Auto-connect requested using saved Wi-Fi credentials");
		return 0;
	#else
		LOG_ERR("CONFIG_WIFI_CREDENTIALS_CONNECT_STORED is not enabled");
		return -ENOTSUP;
	#endif
		
}

static int publisher(void)
{
	/*
	int i, rc, r = 0;

	include_topic = true;
	aliases_enabled = false;

	LOG_INF("attempting to connect: ");
	rc = try_to_connect(&client_ctx);
	PRINT_RESULT("try_to_connect", rc);
	SUCCESS_OR_EXIT(rc);

	i = 0;
	while (i++ < CONFIG_NET_SAMPLE_APP_MAX_ITERATIONS && connected) {
		r = -1;

		rc = mqtt_ping(&client_ctx);
		PRINT_RESULT("mqtt_ping", rc);
		SUCCESS_OR_BREAK(rc);

		rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
		SUCCESS_OR_BREAK(rc);

		rc = publish(&client_ctx, MQTT_QOS_0_AT_MOST_ONCE);
		PRINT_RESULT("mqtt_publish", rc);
		SUCCESS_OR_BREAK(rc);

		rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
		SUCCESS_OR_BREAK(rc);

		rc = publish(&client_ctx, MQTT_QOS_1_AT_LEAST_ONCE);
		PRINT_RESULT("mqtt_publish", rc);
		SUCCESS_OR_BREAK(rc);

		rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
		SUCCESS_OR_BREAK(rc);

		rc = publish(&client_ctx, MQTT_QOS_2_EXACTLY_ONCE);
		PRINT_RESULT("mqtt_publish", rc);
		SUCCESS_OR_BREAK(rc);

		rc = process_mqtt_and_sleep(&client_ctx, APP_SLEEP_MSECS);
		SUCCESS_OR_BREAK(rc);

		r = 0;
	}

	rc = mqtt_disconnect(&client_ctx, NULL);
	PRINT_RESULT("mqtt_disconnect", rc);

	LOG_INF("Bye!");

	return r;
	*/
	return 0;
}

static int start_app(void)
{
	int r = 0, i = 0;

	while (!CONFIG_NET_SAMPLE_APP_MAX_CONNECTIONS ||
		   i++ < CONFIG_NET_SAMPLE_APP_MAX_CONNECTIONS)
	{
		LOG_INF("Publishing...");
		r = publisher();

		if (!CONFIG_NET_SAMPLE_APP_MAX_CONNECTIONS)
		{
			k_sleep(K_MSEC(5000));
		}
	}

	return r;
}

int main(void)
{
	// int ret;
	// bool led_state = false;

	// if (!gpio_is_ready_dt(&led)) {
	// 	return 0;
	// }

	// ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	// if (ret < 0) {
	// 	return 0;
	// }

	// ret = gpio_pin_set_dt(&led, led_state);
	// if (ret < 0) {
	// 	return 0;
	// }

	// printf("LED state: %s\n", led_state ? "ON" : "OFF");

	// while (1) {
	// 	ret = gpio_pin_toggle_dt(&led);
	// 	if (ret < 0) {
	// 		return 0;
	// 	}

	// 	led_state = !led_state;
	// 	printf("LED state: %s\n", led_state ? "ON" : "OFF");
	// 	k_msleep(SLEEP_TIME_MS);
	// }
	int ret = wifi_autoconnect_from_nvs();
	if (ret < 0)
	{
		LOG_WRN("Wi-Fi autoconnect skipped (%d)", ret);
	}
	

	wait_for_network();

	{
		struct net_if *iface = net_if_get_default();
		if (!iface)
		{
			LOG_INF("No default network interface");
		}
		else
		{
			struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
			char buf[NET_IPV6_ADDR_LEN];

			struct in_addr *addr = &ipv4->unicast[0].ipv4.address.net_in_addr;
			// Conversión segura a string
			// sys_le32_to_cpu((net_in_addr)addr.s_addr); // Opcional si ya es host-endian
			net_addr_ntop(AF_INET, &(addr->s_addr), buf, sizeof(buf));

			// Impresión del resultado
			LOG_INF("Dirección IP: %s", buf);

			// struct net_if_ipv4 *ipv4_cfg;

			// struct net_if_addr *ipv4 = net_if_ipv4_addr_get(iface, NET_ADDR_DHCP);
			// int ipv4_res = net_if_config_ipv4_get(iface, &ipv4_cfg);
			// if (!ipv4_res)
			// {
			// 	ipv4 = net_if_ipv4_addr_get(iface, NET_ADDR_MANUAL);
			// }
			// if (ipv4_res)
			// {
			// 	net_addr_ntop(AF_INET, &ipv4->address.in_addr, buf, sizeof(buf));
			// 	LOG_INF("IPv4: %s", buf);
			// }
			// else
			// {
			// 	LOG_INF("IPv4 address not found");
			// }

			/*
struct net_if_addr *ipv6 = net_if_ipv6_addr_get(iface, NET_ADDR_DHCP);
if (!ipv6) {
	ipv6 = net_if_ipv6_addr_get(iface, NET_ADDR_MANUAL);
}
if (ipv6) {
	net_addr_ntop(AF_INET6, &ipv6->address.in6_addr, buf, sizeof(buf));
	LOG_INF("IPv6: %s", log_strdup(buf));
} else {
	LOG_INF("IPv6 address not found");
}
*/
		}
	}

	LOG_INF("Connected to Wi-Fi");

	exit(start_app());

	return 0;
}
