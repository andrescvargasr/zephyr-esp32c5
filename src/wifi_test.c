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

#ifdef CONFIG_XIAO_ESP32C5_LED
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

#define APP_TOPIC_ALIAS 1

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

static void clear_fds(void)
{
	nfds = 0;
}

static int wait(int timeout)
{
	int ret = 0;

	if (nfds > 0)
	{
		ret = poll(fds, nfds, timeout);
		if (ret < 0)
		{
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

void mqtt_evt_handler(struct mqtt_client *const client,
					  const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type)
	{
	case MQTT_EVT_CONNACK:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		connected = true;
		LOG_INF("MQTT client connected!");

#if defined(CONFIG_MQTT_VERSION_5_0)
		if (evt->param.connack.prop.rx.has_topic_alias_maximum &&
			evt->param.connack.prop.topic_alias_maximum > 0)
		{
			LOG_INF("Topic aliases allowed by the broker, max %u.",
					evt->param.connack.prop.topic_alias_maximum);

			aliases_enabled = true;
		}
		else
		{
			LOG_INF("Topic aliases disallowed by the broker.");
		}
#endif

#if defined(CONFIG_LOG_BACKEND_MQTT)
		log_backend_mqtt_client_set(client);
#endif

		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);

		connected = false;
		clear_fds();

#if defined(CONFIG_LOG_BACKEND_MQTT)
		log_backend_mqtt_client_set(NULL);
#endif

		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);

		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id};

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0)
		{
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u",
				evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	default:
		break;
	}
}

static char *get_mqtt_payload(enum mqtt_qos qos)
{
#if APP_BLUEMIX_TOPIC
	static char payload[30];

	snprintk(payload, sizeof(payload), "{d:{temperature:%d}}",
			 sys_rand8_get());
#else
	static char payload[] = "DOORS:OPEN_QoSx";

	payload[strlen(payload) - 1] = '0' + qos;
#endif

	return payload;
}

static char *get_mqtt_topic(void)
{
#if APP_BLUEMIX_TOPIC
	return "iot-2/type/" BLUEMIX_DEVTYPE "/id/" BLUEMIX_DEVID
		   "/evt/" BLUEMIX_EVENT "/fmt/" BLUEMIX_FORMAT;
#else
	return "sensors";
#endif
}

static int publish(struct mqtt_client *client, enum mqtt_qos qos)
{
	struct mqtt_publish_param param = {0};

	/* Always true for MQTT 3.1.1.
	 * True only on first publish message for MQTT 5.0 if broker allows aliases.
	 */
	if (include_topic)
	{
		param.message.topic.topic.utf8 = (uint8_t *)get_mqtt_topic();
		param.message.topic.topic.size =
			strlen(param.message.topic.topic.utf8);
	}

	param.message.topic.qos = qos;
	param.message.payload.data = get_mqtt_payload(qos);
	param.message.payload.len =
		strlen(param.message.payload.data);
	param.message_id = sys_rand16_get();
	param.dup_flag = 0U;
	param.retain_flag = 0U;

#if defined(CONFIG_MQTT_VERSION_5_0)
	if (aliases_enabled)
	{
		param.prop.topic_alias = APP_TOPIC_ALIAS;
		include_topic = false;
	}
#endif

	return mqtt_publish(client, &param);
}

#define RC_STR(rc) ((rc) == 0 ? "OK" : "ERROR")

#define PRINT_RESULT(func, rc) \
	LOG_INF("%s: %d <%s>", (func), rc, RC_STR(rc))

static void broker_init(void)
{
#if defined(CONFIG_NET_IPV6)
	struct sockaddr_in6 *broker6 = (struct sockaddr_in6 *)&broker;

	broker6->sin6_family = AF_INET6;
	broker6->sin6_port = htons(SERVER_PORT);
	inet_pton(AF_INET6, SERVER_ADDR, &broker6->sin6_addr);

#if defined(CONFIG_SOCKS)
	struct sockaddr_in6 *proxy6 = (struct sockaddr_in6 *)&socks5_proxy;

	proxy6->sin6_family = AF_INET6;
	proxy6->sin6_port = htons(SOCKS5_PROXY_PORT);
	inet_pton(AF_INET6, SOCKS5_PROXY_ADDR, &proxy6->sin6_addr);
#endif
#else
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(SERVER_PORT);
	inet_pton(AF_INET, server_addr, &broker4->sin_addr);
	// inet_pton(AF_INET, SERVER_ADDR, &broker4->sin_addr);
#if defined(CONFIG_SOCKS)
	struct sockaddr_in *proxy4 = (struct sockaddr_in *)&socks5_proxy;

	proxy4->sin_family = AF_INET;
	proxy4->sin_port = htons(SOCKS5_PROXY_PORT);
	inet_pton(AF_INET, SOCKS5_PROXY_ADDR, &proxy4->sin_addr);
#endif
#endif
}

static void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	broker_init();

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
	client->client_id.size = strlen(MQTT_CLIENTID);
	client->password = NULL;
	client->user_name = NULL;
#if defined(CONFIG_MQTT_VERSION_5_0)
	client->protocol_version = MQTT_VERSION_5_0;
#else
	client->protocol_version = MQTT_VERSION_3_1_1;
#endif

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
#if defined(CONFIG_MQTT_LIB_TLS)
#if defined(CONFIG_MQTT_LIB_WEBSOCKET)
	client->transport.type = MQTT_TRANSPORT_SECURE_WEBSOCKET;
#else
	client->transport.type = MQTT_TRANSPORT_SECURE;
#endif

	struct mqtt_sec_config *tls_config = &client->transport.tls.config;

	tls_config->peer_verify = TLS_PEER_VERIFY_REQUIRED;
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_list = m_sec_tags;
	tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
#if defined(CONFIG_MBEDTLS_X509_CRT_PARSE_C) || defined(CONFIG_NET_SOCKETS_OFFLOAD)
	tls_config->hostname = TLS_SNI_HOSTNAME;
#else
	tls_config->hostname = NULL;
#endif

#else
#if defined(CONFIG_MQTT_LIB_WEBSOCKET)
	client->transport.type = MQTT_TRANSPORT_NON_SECURE_WEBSOCKET;
#else
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif
#endif

#if defined(CONFIG_MQTT_LIB_WEBSOCKET)
	client->transport.websocket.config.host = SERVER_ADDR;
	client->transport.websocket.config.url = "/mqtt";
	client->transport.websocket.config.tmp_buf = temp_ws_rx_buf;
	client->transport.websocket.config.tmp_buf_len =
		sizeof(temp_ws_rx_buf);
	client->transport.websocket.timeout = 5 * MSEC_PER_SEC;
#endif

#if defined(CONFIG_SOCKS)
	mqtt_client_set_proxy(client, &socks5_proxy,
						  socks5_proxy.sa_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6));
#endif
}

/* In this routine we block until the connected variable is 1 */
static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected)
	{

		client_init(client);

		rc = mqtt_connect(client);
		if (rc != 0)
		{
			PRINT_RESULT("mqtt_connect", rc);
			k_sleep(K_MSEC(APP_SLEEP_MSECS));
			continue;
		}

		prepare_fds(client);

		if (wait(APP_CONNECT_TIMEOUT_MS))
		{
			mqtt_input(client);
		}

		if (!connected)
		{
			mqtt_abort(client);
		}
	}

	if (connected)
	{
		return 0;
	}

	return -EINVAL;
}

static int process_mqtt_and_sleep(struct mqtt_client *client, int timeout)
{
	int64_t remaining = timeout;
	int64_t start_time = k_uptime_get();
	int rc;

	while (remaining > 0 && connected)
	{
		if (wait(remaining))
		{
			rc = mqtt_input(client);
			if (rc != 0)
			{
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		rc = mqtt_live(client);
		if (rc != 0 && rc != -EAGAIN)
		{
			PRINT_RESULT("mqtt_live", rc);
			return rc;
		}
		else if (rc == 0)
		{
			rc = mqtt_input(client);
			if (rc != 0)
			{
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		remaining = timeout + start_time - k_uptime_get();
	}

	return 0;
}

#define SUCCESS_OR_EXIT(rc) \
	{                       \
		if (rc != 0)        \
		{                   \
			return 1;       \
		}                   \
	}
#define SUCCESS_OR_BREAK(rc) \
	{                        \
		if (rc != 0)         \
		{                    \
			break;           \
		}                    \
	}

static int publisher(void)
{
	int i, rc, r = 0;

	include_topic = true;
	aliases_enabled = false;

	LOG_INF("attempting to connect: ");
	rc = try_to_connect(&client_ctx);
	PRINT_RESULT("try_to_connect", rc);
	SUCCESS_OR_EXIT(rc);

	i = 450;
	while (i++ < CONFIG_NET_SAMPLE_APP_MAX_ITERATIONS && connected)
	{
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
			k_sleep(K_MSEC(20000));
		}
	}

	return r;
}

/** The system work queue is used to handle periodic MQTT publishing.
 *  Work queuing begins when the MQTT connection is established.
 *  Use CONFIG_NET_SAMPLE_MQTT_PUBLISH_INTERVAL to set the publish frequency.
 */

static void publish_work_handler(struct k_work *work)
{
	int rc;

	if (mqtt_connected)
	{
		rc = app_mqtt_publish(&client_ctx);
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

#ifdef CONFIG_XIAO_ESP32C5_LED
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
#ifdef CONFIG_XIAO_ESP32C5_LED
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
