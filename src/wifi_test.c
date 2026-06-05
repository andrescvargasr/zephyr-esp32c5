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

#include "mqtt_client.h"

#ifdef CONFIG_BOARD_XIAO_ESP32C5
/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 1000
/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#endif

/* MQTT publish work item */
struct k_work_delayable mqtt_publish_work;

static char server_addr[INET_ADDRSTRLEN] = SERVER_ADDR;

/* Buffers for MQTT client. */
static uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

/* The mqtt client struct */
static struct mqtt_client client_ctx;

/* MQTT Broker details. */
static struct sockaddr_storage broker;

static struct pollfd fds[1];
static int nfds;
static bool connected;

/* Whether to include full topic in the publish message, or alias only (MQTT 5). */
static bool include_topic;
static bool aliases_enabled;

static int wifi_autoconnect_from_nvs(void)
{
	struct net_if *iface = net_if_get_default();
	int ret;

	if (iface == NULL)
	{
		LOG_ERR("No default network interface");
		return -ENODEV;
	}

	if (wifi_credentials_is_empty())
	{
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

static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_NON_SECURE)
	{
		fds[0].fd = client->transport.tcp.sock;
	}
#if defined(CONFIG_MQTT_LIB_TLS)
	else if (client->transport.type == MQTT_TRANSPORT_SECURE)
	{
		fds[0].fd = client->transport.tls.sock;
	}
#endif

	fds[0].events = POLLIN;
	nfds = 1;
}

/** The system work queue is used to handle periodic MQTT publishing.
 *  Work queuing begins when the MQTT connection is established.
 *  Use CONFIG_NET_SAMPLE_MQTT_PUBLISH_INTERVAL to set the publish frequency.
 */

static void publish_work_handler(struct k_work *work)
{
	int rc;
	static bool first_msg = true;

	if (mqtt_connected)
	{
		if (first_msg)
		{
			LOG_INF("Publishing first MQTT message...");
			first_msg = false;
			// rc = app_mqtt_publish(&client_ctx);
			rc = app_mqtt_publish_status(&client_ctx);
			rc += app_mqtt_publish_mode(&client_ctx);
			rc += app_mqtt_publish_targets(&client_ctx);
			rc += app_mqtt_publish_descriptor(&client_ctx);
		}
		else
		{
			rc = app_mqtt_publish_parameters(&client_ctx);
		}
		if (rc != 0)
		{
			LOG_INF("MQTT Publish failed [%d]", rc);
		}
		k_work_reschedule(&mqtt_publish_work,
						  K_SECONDS(CONFIG_NET_SAMPLE_MQTT_PUBLISH_INTERVAL));
	}
	else
	{
		k_work_cancel_delayable(&mqtt_publish_work);
	}
}

int main(void)
{
	int ret;

#ifdef CONFIG_BOARD_XIAO_ESP32C5
	LOG_INF("Starting Wi-Fi test with LED indication");
	bool led_state = false;

	if (!gpio_is_ready_dt(&led))
	{
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0)
	{
		return 0;
	}

	ret = gpio_pin_set_dt(&led, led_state);
	if (ret < 0)
	{
		return 0;
	}

	LOG_INF("LED state: %s", led_state ? "ON" : "OFF");
#else
	LOG_INF("Starting Wi-Fi test without LED indication");
#endif

	ret = wifi_autoconnect_from_nvs();
	if (ret < 0)
	{
		LOG_WRN("Wi-Fi autoconnect skipped (%d)", ret);
	}

	wait_for_network();

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

		if (strcmp(server_addr, NO_SERVER_ADDR) == 0)
		{
			LOG_WRN("SERVER_ADDR is empty, assigning new value");
			char new_server_addr[INET_ADDRSTRLEN] = DEFAULT_SERVER_ADDR;
			strncpy(server_addr, new_server_addr, sizeof(new_server_addr));
			LOG_INF("Broker address: %s", server_addr);
		}
		else
		{
			LOG_INF("Broker address: %s", server_addr);
		}

		int rc = app_mqtt_init(&client_ctx, server_addr);
		if (rc != 0)
		{
			LOG_ERR("MQTT Init failed [%d]", rc);
			return rc;
		}

		/* Initialise MQTT publish work item */
		k_work_init_delayable(&mqtt_publish_work, publish_work_handler);

		/* Thread main loop */
		while (1)
		{

#ifdef CONFIG_BOARD_XIAO_ESP32C5
			ret = gpio_pin_toggle_dt(&led);
			if (ret < 0)
			{
				return 0;
			}

			led_state = !led_state;
			LOG_INF("LED state: %s", led_state ? "ON" : "OFF");
#endif

			/* Block until MQTT connection is up */
			app_mqtt_connect(&client_ctx);

			/* We are now connected, begin queueing periodic MQTT publishes */
			k_work_reschedule(&mqtt_publish_work,
							  K_SECONDS(CONFIG_NET_SAMPLE_MQTT_PUBLISH_INTERVAL));

			/* Handle MQTT inputs and connection */
			app_mqtt_run(&client_ctx);
			k_msleep(SLEEP_TIME_MS);
		}
	}

	LOG_INF("Connected to Wi-Fi");

	// exit(start_app());

	return 0;
}
