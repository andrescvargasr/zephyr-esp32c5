/*
 * Copyright (c) 2024 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app_mqtt, LOG_LEVEL_DBG);

#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/netdb.h>
#include <zephyr/posix/poll.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/random/random.h>

#include "mqtt_client.h"
#include "config.h"

static char device_array[] = DEVICE_DESCRIPTOR_MACRO;

/* Buffers for MQTT client */
static uint8_t rx_buffer[CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE];
static uint8_t tx_buffer[CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE];

/* MQTT payload buffer */
static uint8_t payload_buf[CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE];
static uint8_t payload_dev_descriptor_buf[CONFIG_TECHSTIM_DEV_DESC_PAYLOAD_SIZE];
static uint8_t payload_parameters_buf[CONFIG_TECHSTIM_DEV_DESC_PAYLOAD_SIZE];

/* MQTT broker details */
static struct sockaddr_storage broker;

/* Socket descriptor */
static struct pollfd fds[1];
static int nfds;

/** @brief Sensor sample structure */
struct sensor_sample
{
	const char *unit;
	int value;
};

struct info_status
{
	const char *status;
};

struct info_mode
{
	char *mode;
};

struct info_targets
{
	char *targets;
};

/* ejemplo de inicialización con los valores del JSON */
static device_descriptor_t device = {
	.Length = 38,
	.DescriptorVersion = 1024,
	.DeviceClass = 0,
	.DeviceSubClass = 1,
	.DeviceID = 1,
	.DeviceProtocol = 2,
	.MaxPacketSize = 13,
	.Vendor = 12345,
	.Product = 13,
	.BCDDevice = 1024,
	.SerialNumber = 123,
	.IDs = 16,
	.HardwareVersion = 4096,
	.FirmwareVersion = 1024,
	.ProtocolsSupported = 13,
	.WIP = 0xC0A80168, /* opcional: htonl(…) si se transmite */
	.CANID = 0,
	.MAC = 0x001B44113AB7ULL,
	.Port = 3000,
	.ByteSpeed = 115200,
};

static const pos_amp_t pos_amp = {
	.pos_amp = {0, 0, 0, 0},
	.pos_amp_len = 4,
};

static const neg_amp_t neg_amp = {
	.neg_amp = {0, 0, 0, 0},
	.neg_amp_len = 4,
};

static const pos_time_t pos_time = {
	.pos_time = {0, 0, 0, 0},
	.pos_time_len = 4,
};

static const neg_time_t neg_time = {
	.neg_time = {0, 0, 0, 0},
	.neg_time_len = 4,
};

static const umbral_motor_t umbral_motor = {
	.motor_amp = {0, 0, 0, 0},
	.motor_amp_len = 4,
};

static const umbral_comfort_t umbral_comfort = {
	.comfort_amp = {0, 0, 0, 0},
	.comfort_amp_len = 4,
};

static const max_current_t max_current = {
	.max_amp = {0, 0, 0, 0},
	.max_amp_len = 4,
};

static const min_current_t min_current = {
	.min_amp = {0, 0, 0, 0},
	.min_amp_len = 4,
};

static const parameters_t PARAMETERS_T = {
	.pos_amp = pos_amp,
	.neg_amp = neg_amp,
	.pos_time = pos_time,
	.neg_time = neg_time,
	.order_channels = (1 << 0) | (0 << 2) | (3 << 4) | (2 << 6), // Ejemplo: orden de canales 1-0-3-2
	.repetitions = (1 << 0) | (2 << 2) | (0 << 4) | (0 << 6),	 // Ejemplo: doble repetición para canal 1, triple para canal 2, simple para canal 3
	.inv_pulse = (1 << 0) | (0 << 1) | (1 << 2) | (0 << 3),		 // Ejemplo: invertir pulso para canales 1 y 3
	.umbral_motor = umbral_motor,
	.umbral_comfort = umbral_comfort,
	.min_current = min_current,
	.max_current = max_current,
	.intra_freq = 10,
	.group_freq = 50,
	.upgrade_ramp = 100,
	.downgrade_ramp = 100,
	.stimulation_mode = (0 << 0) | (1 << 2) | (0 << 4) | (1 << 6), // Ejemplo: modo asimétrico para canal 1, simétrico para canal 2
	.delay = 100,
	.device_id = 5341,
	.firmware_version = SOFTWARE_VERSION_SEMVER, // Ejemplo: versión 2.0.0
	.error = 0,
	.status = 0,
	.enabled = 0,
	.log_counter = (MAX_PARAMS - 1),
	.qualifier = 5341,
};

/* JSON payload format */
static const struct json_obj_descr sensor_sample_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct sensor_sample, unit, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct sensor_sample, value, JSON_TOK_NUMBER),
};

static const struct json_obj_descr info_status_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct info_status, status, JSON_TOK_STRING),
};

static const struct json_obj_descr info_mode_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct info_mode, mode, JSON_TOK_STRING),
};

static const struct json_obj_descr info_targets_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct info_targets, targets, JSON_TOK_STRING),
};

/* JSON descriptor for device_descriptor_t */
static const struct json_obj_descr device_descriptor_descr[] = {
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, Length, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, DescriptorVersion, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, DeviceClass, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, DeviceSubClass, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, DeviceID, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, DeviceProtocol, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, MaxPacketSize, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, Vendor, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, Product, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, BCDDevice, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, SerialNumber, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, IDs, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, HardwareVersion, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, FirmwareVersion, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, ProtocolsSupported, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, WIP, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, CANID, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, MAC, JSON_TOK_UINT64),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, Port, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(device_descriptor_t, ByteSpeed, JSON_TOK_UINT),
};

/* JSON descriptor for parameters_t */
static const struct json_obj_descr parameters_descr[] = {
	JSON_OBJ_DESCR_ARRAY(pos_amp_t, pos_amp, 4, pos_amp_len, JSON_TOK_UINT),
	JSON_OBJ_DESCR_ARRAY(neg_amp_t, neg_amp, 4, neg_amp_len, JSON_TOK_UINT),
	JSON_OBJ_DESCR_ARRAY(pos_time_t, pos_time, 4, pos_time_len, JSON_TOK_UINT),
	JSON_OBJ_DESCR_ARRAY(neg_time_t, neg_time, 4, neg_time_len, JSON_TOK_UINT),

	JSON_OBJ_DESCR_PRIM(parameters_t, order_channels, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, repetitions, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, inv_pulse, JSON_TOK_UINT),

	JSON_OBJ_DESCR_ARRAY(umbral_motor_t, motor_amp, 4, motor_amp_len, JSON_TOK_UINT),
	JSON_OBJ_DESCR_ARRAY(umbral_comfort_t, comfort_amp, 4, comfort_amp_len, JSON_TOK_UINT),
	JSON_OBJ_DESCR_ARRAY(min_current_t, min_amp, 4, min_amp_len, JSON_TOK_UINT),
	JSON_OBJ_DESCR_ARRAY(max_current_t, max_amp, 4, max_amp_len, JSON_TOK_UINT),

	// JSON_OBJ_DESCR_PRIM(parameters_t, umbral_motor, JSON_TOK_UINT),
	// JSON_OBJ_DESCR_PRIM(parameters_t, umbral_comfort, JSON_TOK_UINT),
	// JSON_OBJ_DESCR_PRIM(parameters_t, min_current, JSON_TOK_UINT),
	// JSON_OBJ_DESCR_PRIM(parameters_t, max_current, JSON_TOK_UINT),

	JSON_OBJ_DESCR_PRIM(parameters_t, intra_freq, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, group_freq, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, upgrade_ramp, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, downgrade_ramp, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, stimulation_mode, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, delay, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, device_id, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, firmware_version, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, error, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, status, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, enabled, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, log_counter, JSON_TOK_UINT),
	JSON_OBJ_DESCR_PRIM(parameters_t, qualifier, JSON_TOK_UINT),
};

/* MQTT connectivity status flag */
bool mqtt_connected;

/* MQTT client ID buffer */
static uint8_t client_id[50];

/* MQTT Last Will typedef struct */
typedef struct
{
	struct mqtt_topic topic;  /**< Last will topic with QoS. */
	struct mqtt_utf8 message; /**< Last will message payload. */
	bool retain;			  /**< Retain flag for last will message. */
} mqtt_will_t;

/* MQTT Last Will configuration */
static const char will_topic[] = CONFIG_NET_SAMPLE_MQTT_PUB_TOPIC "/" SERIAL_NUMBER "/info/status";
static const char will_message[] = "disconnected";
static mqtt_will_t will_param = {
	.topic = {
		.topic = MQTT_UTF8_LITERAL(CONFIG_NET_SAMPLE_MQTT_PUB_TOPIC "/" SERIAL_NUMBER "/info/status"),
		.qos = 1,
	},
	.message = MQTT_UTF8_LITERAL("disconnected"),
	.retain = true,
};

/** Retrieves a sensor sample and encodes it in JSON format */
static int get_mqtt_payload(struct mqtt_binstr *payload)
{
	int rc;
	struct sensor_sample sample = {
		.unit = "C",
		.value = sys_rand32_get() % 100};

	rc = 0;
	// rc = device_read_sensor(&sample);
	if (rc != 0)
	{
		LOG_ERR("Failed to get sensor sample [%d]", rc);
		return rc;
	}

	rc = json_obj_encode_buf(sensor_sample_descr, ARRAY_SIZE(sensor_sample_descr),
							 &sample, payload_buf, CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE);
	if (rc != 0)
	{
		LOG_ERR("Failed to encode JSON object [%d]", rc);
		return rc;
	}

	payload->data = payload_buf;
	payload->len = strlen(payload->data);

	return rc;
}

/** Retrieves a sensor sample and encodes it in JSON format */
static int get_mqtt_payload_status(struct mqtt_binstr *payload)
{
	int rc;
	struct info_status status = {
		.status = INFO_STATUS_CONNECTED};

	rc = 0;
	if (rc != 0)
	{
		LOG_ERR("Failed to get sensor sample [%d]", rc);
		return rc;
	}

	rc = json_obj_encode_buf(info_status_descr, ARRAY_SIZE(info_status_descr),
							 &status, payload_buf, CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE);
	if (rc != 0)
	{
		LOG_ERR("Failed to encode JSON object [%d]", rc);
		return rc;
	}

	payload->data = payload_buf;
	payload->len = strlen(payload->data);

	return rc;
}

/** Retrieves a sensor sample and encodes it in JSON format */
static int get_mqtt_payload_mode(struct mqtt_binstr *payload)
{
	int rc;
	struct info_mode mode = {
		.mode = INFO_MODE_CONTROLLER};

	rc = 0;
	if (rc != 0)
	{
		LOG_ERR("Failed to get sensor sample [%d]", rc);
		return rc;
	}

	rc = json_obj_encode_buf(info_mode_descr, ARRAY_SIZE(info_mode_descr),
							 &mode, payload_buf, CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE);
	if (rc != 0)
	{
		LOG_ERR("Failed to encode JSON object [%d]", rc);
		return rc;
	}

	payload->data = payload_buf;
	payload->len = strlen(payload->data);

	return rc;
}

/** Retrieves a targets and encodes it in JSON format */
static int get_mqtt_payload_targets(struct mqtt_binstr *payload)
{
	int rc;
	struct info_targets targets =
		{
			.targets = "[" SERIAL_NUMBER "," SERIAL_NUMBER "]"};

	rc = 0;
	if (rc != 0)
	{
		LOG_ERR("Failed to get sensor sample [%d]", rc);
		return rc;
	}

	rc = json_obj_encode_buf(info_targets_descr, ARRAY_SIZE(info_targets_descr),
							 &targets, payload_buf, CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE);
	if (rc != 0)
	{
		LOG_ERR("Failed to encode JSON object [%d]", rc);
		return rc;
	}

	payload->data = payload_buf;
	payload->len = strlen(payload->data);

	return rc;
}

/** Retrieves a targets and encodes it in JSON format */
static int get_mqtt_payload_descriptor(struct mqtt_binstr *payload)
{
	int rc;
	/* ejemplo de inicialización con los valores del JSON */
	static device_descriptor_t device = {
		.Length = 38,
		.DescriptorVersion = 1024,
		.DeviceClass = 0,
		.DeviceSubClass = 1,
		.DeviceID = 1,
		.DeviceProtocol = 2,
		.MaxPacketSize = 13,
		.Vendor = 12345,
		.Product = 13,
		.BCDDevice = 1024,
		.SerialNumber = 123,
		.IDs = 16,
		.HardwareVersion = 4096,
		.FirmwareVersion = 1024,
		.ProtocolsSupported = 13,
		.WIP = 0xC0A80168, /* opcional: htonl(…) si se transmite */
		.CANID = 0,
		.MAC = 0x001B44113AB7ULL,
		.Port = 3000,
		.ByteSpeed = 115200,
	};

	rc = 0;
	if (rc != 0)
	{
		LOG_ERR("Failed to get sensor sample [%d]", rc);
		return rc;
	}

	rc = json_obj_encode_buf(device_descriptor_descr, ARRAY_SIZE(device_descriptor_descr),
							 &device, payload_dev_descriptor_buf, CONFIG_TECHSTIM_DEV_DESC_PAYLOAD_SIZE);
	if (rc != 0)
	{
		LOG_ERR("Failed to encode JSON object [%d]", rc);
		return rc;
	}

	payload->data = payload_dev_descriptor_buf;
	payload->len = strlen(payload->data);

	return rc;
}

/** Retrieves a targets and encodes it in JSON format */
static int get_mqtt_payload_parameters(struct mqtt_binstr *payload)
{
	int rc;

	rc = 0;
	if (rc != 0)
	{
		LOG_ERR("Failed to get sensor sample [%d]", rc);
		return rc;
	}

	rc = json_obj_encode_buf(parameters_descr, ARRAY_SIZE(parameters_descr),
							 &PARAMETERS_T, payload_parameters_buf, CONFIG_TECHSTIM_DEV_DESC_PAYLOAD_SIZE);
	if (rc != 0)
	{
		LOG_ERR("Failed to encode JSON object [%d]", rc);
		return rc;
	}

	LOG_DBG("Encoded parameters JSON length: %d", strlen(payload_parameters_buf));

	payload->data = payload_parameters_buf;
	payload->len = strlen(payload->data);

	return rc;
}

static void clear_fds(void)
{
	nfds = 0;
}

static inline void on_mqtt_disconnect(void)
{
	mqtt_connected = false;
	clear_fds();
	// device_write_led(LED_NET, LED_OFF);
	LOG_INF("Disconnected from MQTT broker");
}

static inline void on_mqtt_connect(void)
{
	mqtt_connected = true;
	// device_write_led(LED_NET, LED_ON);
	LOG_INF("Connected to MQTT broker!");
	LOG_INF("Hostname: %s", CONFIG_NET_SAMPLE_MQTT_BROKER_HOSTNAME);
	LOG_INF("Client ID: %s", client_id);
	LOG_INF("Port: %s", CONFIG_NET_SAMPLE_MQTT_BROKER_PORT);
	LOG_INF("TLS: %s",
			IS_ENABLED(CONFIG_MQTT_LIB_TLS) ? "Enabled" : "Disabled");
}

int app_mqtt_publish(struct mqtt_client *client)
{
	int rc;
	struct mqtt_publish_param param;
	struct mqtt_binstr payload;
	static uint16_t msg_id = 1;
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = CONFIG_NET_SAMPLE_MQTT_PUB_TOPIC "/" SERIAL_NUMBER,
			.size = strlen(topic.topic.utf8)},
		.qos = IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_0_AT_MOST_ONCE) ? 0 : (IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_1_AT_LEAST_ONCE) ? 1 : 2)};

	rc = get_mqtt_payload(&payload);
	if (rc != 0)
	{
		LOG_ERR("Failed to get MQTT payload [%d]", rc);
	}

	param.message.topic = topic;
	param.message.payload = payload;
	param.message_id = msg_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;

	rc = mqtt_publish(client, &param);
	if (rc != 0)
	{
		LOG_ERR("MQTT Publish failed [%d]", rc);
	}

	LOG_INF("Published to topic '%s', QoS %d",
			param.message.topic.topic.utf8,
			param.message.topic.qos);

	return rc;
}

int app_mqtt_publish_status(struct mqtt_client *client)
{
	int rc;
	struct mqtt_publish_param param;
	struct mqtt_binstr payload;
	static uint16_t msg_id = 1;
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = CONFIG_NET_SAMPLE_MQTT_PUB_TOPIC "/" SERIAL_NUMBER "/info/status",
			.size = strlen(topic.topic.utf8)},
		.qos = IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_0_AT_MOST_ONCE) ? 0 : (IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_1_AT_LEAST_ONCE) ? 1 : 2)};

	rc = get_mqtt_payload_status(&payload);
	if (rc != 0)
	{
		LOG_ERR("Failed to get MQTT payload [%d]", rc);
	}

	param.message.topic = topic;
	param.message.payload = payload;
	param.message_id = msg_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;

	rc = mqtt_publish(client, &param);
	if (rc != 0)
	{
		LOG_ERR("MQTT Publish failed [%d]", rc);
	}

	LOG_INF("Published to topic '%s', QoS %d",
			param.message.topic.topic.utf8,
			param.message.topic.qos);

	return rc;
}

int app_mqtt_publish_mode(struct mqtt_client *client)
{
	int rc;
	struct mqtt_publish_param param;
	struct mqtt_binstr payload;
	static uint16_t msg_id = 1;
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = CONFIG_NET_SAMPLE_MQTT_PUB_TOPIC "/" SERIAL_NUMBER "/info/mode",
			.size = strlen(topic.topic.utf8)},
		.qos = IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_0_AT_MOST_ONCE) ? 0 : (IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_1_AT_LEAST_ONCE) ? 1 : 2)};

	rc = get_mqtt_payload_mode(&payload);
	if (rc != 0)
	{
		LOG_ERR("Failed to get MQTT payload [%d]", rc);
	}

	param.message.topic = topic;
	param.message.payload = payload;
	param.message_id = msg_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;

	rc = mqtt_publish(client, &param);
	if (rc != 0)
	{
		LOG_ERR("MQTT Publish failed [%d]", rc);
	}

	LOG_INF("Published to topic '%s', QoS %d",
			param.message.topic.topic.utf8,
			param.message.topic.qos);

	return rc;
}

int app_mqtt_publish_targets(struct mqtt_client *client)
{
	int rc;
	struct mqtt_publish_param param;
	struct mqtt_binstr payload;
	static uint16_t msg_id = 1;
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = CONFIG_NET_SAMPLE_MQTT_PUB_TOPIC "/" SERIAL_NUMBER "/info/targets",
			.size = strlen(topic.topic.utf8)},
		.qos = IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_0_AT_MOST_ONCE) ? 0 : (IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_1_AT_LEAST_ONCE) ? 1 : 2)};

	rc = get_mqtt_payload_targets(&payload);
	if (rc != 0)
	{
		LOG_ERR("Failed to get MQTT payload [%d]", rc);
	}

	param.message.topic = topic;
	param.message.payload = payload;
	param.message_id = msg_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;

	rc = mqtt_publish(client, &param);
	if (rc != 0)
	{
		LOG_ERR("MQTT Publish failed [%d]", rc);
	}

	LOG_INF("Published to topic '%s', QoS %d",
			param.message.topic.topic.utf8,
			param.message.topic.qos);

	return rc;
}

int app_mqtt_publish_descriptor(struct mqtt_client *client)
{
	int rc;
	struct mqtt_publish_param param;
	struct mqtt_binstr payload;
	static uint16_t msg_id = 1;
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = CONFIG_NET_SAMPLE_MQTT_PUB_TOPIC "/" SERIAL_NUMBER "/device_descriptor",
			.size = strlen(topic.topic.utf8)},
		.qos = IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_0_AT_MOST_ONCE) ? 0 : (IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_1_AT_LEAST_ONCE) ? 1 : 2)};

	rc = get_mqtt_payload_descriptor(&payload);
	if (rc != 0)
	{
		LOG_ERR("Failed to get MQTT payload [%d]", rc);
	}

	param.message.topic = topic;
	param.message.payload = payload;
	param.message_id = msg_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;

	rc = mqtt_publish(client, &param);
	if (rc != 0)
	{
		LOG_ERR("MQTT Publish failed [%d]", rc);
	}

	LOG_INF("Published to topic '%s', QoS %d",
			param.message.topic.topic.utf8,
			param.message.topic.qos);

	return rc;
}

int app_mqtt_publish_parameters(struct mqtt_client *client)
{
	int rc;
	struct mqtt_publish_param param;
	struct mqtt_binstr payload;
	static uint16_t msg_id = 1;
	struct mqtt_topic topic = {
		.topic = {
			.utf8 = CONFIG_NET_SAMPLE_MQTT_PUB_TOPIC "/" SERIAL_NUMBER "/parameters",
			.size = strlen(topic.topic.utf8)},
		.qos = IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_0_AT_MOST_ONCE) ? 0 : (IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_1_AT_LEAST_ONCE) ? 1 : 2)};

	rc = get_mqtt_payload_parameters(&payload);
	if (rc != 0)
	{
		LOG_ERR("Failed to get MQTT payload [%d]", rc);
	}

	param.message.topic = topic;
	param.message.payload = payload;
	param.message_id = msg_id++;
	param.dup_flag = 0;
	param.retain_flag = 0;

	rc = mqtt_publish(client, &param);
	if (rc != 0)
	{
		LOG_ERR("MQTT Publish failed [%d]", rc);
	}

	LOG_INF("Published to topic '%s', QoS %d",
			param.message.topic.topic.utf8,
			param.message.topic.qos);

	return rc;
}

/** Initialise the MQTT client ID as the board name with random hex postfix */
static void init_mqtt_client_id(void)
{
	snprintk(client_id, sizeof(client_id), SERIAL_NUMBER "_%x", (uint8_t)sys_rand32_get());
}

/** Called when an MQTT payload is received.
 *  Reads the payload and calls the commands
 *  handler if a payloads is received on the
 *  command topic
 */
static void on_mqtt_publish(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	int rc;
	uint8_t payload[CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE];
	uint8_t topic_buf[256]; /* Buffer for null-terminated topic string */
	char device_id[64];		/* Buffer to store extracted device ID */
	char *token, *rest;

	rc = mqtt_read_publish_payload(client, payload,
								   CONFIG_NET_SAMPLE_MQTT_PAYLOAD_SIZE);
	if (rc < 0)
	{
		LOG_ERR("Failed to read received MQTT payload [%d]", rc);
		return;
	}
	/* Place null terminator at end of payload buffer */
	payload[rc] = '\0';

	/* Extract topic with proper null termination */
	uint32_t topic_size = evt->param.publish.message.topic.topic.size;
	if (topic_size >= sizeof(topic_buf))
	{
		topic_size = sizeof(topic_buf) - 1;
	}
	memcpy(topic_buf, evt->param.publish.message.topic.topic.utf8, topic_size);
	topic_buf[topic_size] = '\0';

	LOG_INF("MQTT payload received!");
	LOG_INF("topic: '%s', payload: %s", (char *)topic_buf, payload);

	/* Extract device ID from topic (format: "technaid_sl/techstim/cuX/*") */
	memset(device_id, 0, sizeof(device_id));
	rest = (char *)topic_buf;

	/* Get first token (technaid_sl) */
	token = strtok_r(rest, "/", &rest);
	/* Get second token (techstim) */
	token = strtok_r(NULL, "/", &rest);
	/* Get third token (device ID - cu1, cu2, cu3, cu4, etc.) */
	token = strtok_r(NULL, "/", &rest);
	if (token != NULL)
	{
		strncpy(device_id, token, sizeof(device_id) - 1);
		LOG_INF("Extracted device ID: %s", device_id);
	}

	/* If the topic is a command, call the command handler  */
	if (strcmp((char *)topic_buf, CONFIG_NET_SAMPLE_MQTT_SUB_TOPIC_CMD) == 0)
	{
		LOG_INF("Update device from MQTT command");
		// device_command_handler(payload);
	}
}

/** Handler for asynchronous MQTT events */
static void mqtt_event_handler(struct mqtt_client *const client, const struct mqtt_evt *evt)
{
	switch (evt->type)
	{
	case MQTT_EVT_CONNACK:
		LOG_INF("MQTT_EVT_CONNACK");
		if (evt->result != 0)
		{
			LOG_ERR("MQTT Event Connect failed [%d]", evt->result);
			break;
		}
		on_mqtt_connect();
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT_EVT_DISCONNECT");
		on_mqtt_disconnect();
		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBACK error [%d]", evt->result);
			break;
		}

		LOG_INF("PUBACK packet ID: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBREC error [%d]", evt->result);
			break;
		}

		LOG_INF("PUBREC packet ID: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id};

		mqtt_publish_qos2_release(client, &rel_param);
		break;

	case MQTT_EVT_PUBREL:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBREL error [%d]", evt->result);
			break;
		}

		LOG_INF("PUBREL packet ID: %u", evt->param.pubrel.message_id);

		const struct mqtt_pubcomp_param rec_param = {
			.message_id = evt->param.pubrel.message_id};

		mqtt_publish_qos2_complete(client, &rec_param);
		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet ID: %u", evt->param.pubcomp.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result == MQTT_SUBACK_FAILURE)
		{
			LOG_ERR("MQTT SUBACK error [%d]", evt->result);
			break;
		}

		LOG_INF("SUBACK packet ID: %d", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PUBLISH:
		LOG_INF("MQTT_EVT_PUBLISH");
		const struct mqtt_publish_param *p = &evt->param.publish;

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE)
		{
			const struct mqtt_puback_param ack_param = {
				.message_id = p->message_id};
			mqtt_publish_qos1_ack(client, &ack_param);
		}
		else if (p->message.topic.qos == MQTT_QOS_2_EXACTLY_ONCE)
		{
			const struct mqtt_pubrec_param rec_param = {
				.message_id = p->message_id};
			mqtt_publish_qos2_receive(client, &rec_param);
		}

		on_mqtt_publish(client, evt);

	default:
		break;
	}
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

/** Poll the MQTT socket for received data */
static int poll_mqtt_socket(struct mqtt_client *client, int timeout)
{
	int rc;

	prepare_fds(client);

	if (nfds <= 0)
	{
		return -EINVAL;
	}

	rc = poll(fds, nfds, timeout);
	if (rc < 0)
	{
		LOG_ERR("Socket poll error [%d]", rc);
	}

	return rc;
}

int app_mqtt_subscribe(struct mqtt_client *client)
{
	int rc;
	const char *cmd_topic[4] = {
		// CONFIG_NET_SAMPLE_MQTT_SUB_TOPIC_CMD "/+/status",
		CONFIG_NET_SAMPLE_MQTT_SUB_TOPIC_CMD "/cu1/info/status",
		CONFIG_NET_SAMPLE_MQTT_SUB_TOPIC_CMD "/cu2/info/status",
		CONFIG_NET_SAMPLE_MQTT_SUB_TOPIC_CMD "/cu3/info/status",
		CONFIG_NET_SAMPLE_MQTT_SUB_TOPIC_CMD "/cu4/info/status",
	};

	for (int i = 0; i < 2; i++)
	{
		struct mqtt_topic sub_topics[2] = {0}; // 3 topics is the max supported by Zephyr MQTT library, so we subscribe in batches of 2
		for (int j = 0; j < 2; j++)
		{
			sub_topics[j].topic.utf8 = cmd_topic[(i * 2) + j];
			sub_topics[j].topic.size = strlen(sub_topics[j].topic.utf8);
			sub_topics[j].qos = IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_0_AT_MOST_ONCE) ? 0 : (IS_ENABLED(CONFIG_NET_SAMPLE_MQTT_QOS_1_AT_LEAST_ONCE) ? 1 : 2);
			LOG_INF("Topic %d: %s", (i * 2) + j + 1, sub_topics[j].topic.utf8);
		}
		const struct mqtt_subscription_list sub_list = {
			.list = sub_topics,
			.list_count = ARRAY_SIZE(sub_topics),
			.message_id = 5841u};

		LOG_INF("Subscribing to %d topic(s)", sub_list.list_count);

		rc = mqtt_subscribe(client, &sub_list);
		if (rc != 0)
		{
			LOG_ERR("MQTT Subscribe failed [%d]", rc);
		}
	}

	return rc;
}

/** Process incoming MQTT data and keep the connection alive*/
int app_mqtt_process(struct mqtt_client *client)
{
	int rc;

	rc = poll_mqtt_socket(client, mqtt_keepalive_time_left(client));
	if (rc != 0)
	{
		if (fds[0].revents & POLLIN)
		{
			/* MQTT data received */
			rc = mqtt_input(client);
			if (rc != 0)
			{
				LOG_ERR("MQTT Input failed [%d]", rc);
				return rc;
			}
			/* Socket error */
			if (fds[0].revents & (POLLHUP | POLLERR))
			{
				LOG_ERR("MQTT socket closed / error");
				return -ENOTCONN;
			}
		}
	}
	else
	{
		/* Socket poll timed out, time to call mqtt_live() */
		rc = mqtt_live(client);
		if (rc != 0)
		{
			LOG_ERR("MQTT Live failed [%d]", rc);
			return rc;
		}
	}

	return 0;
}

void app_mqtt_run(struct mqtt_client *client)
{
	int rc;

	/* Subscribe to MQTT topics */
	app_mqtt_subscribe(client);

	/* Thread will primarily remain in this loop */
	while (mqtt_connected)
	{
		rc = app_mqtt_process(client);
		if (rc != 0)
		{
			break;
		}
	}
	/* Gracefully close connection */
	mqtt_disconnect(client, NULL);
}

void app_mqtt_connect(struct mqtt_client *client)
{
	int rc = 0;

	mqtt_connected = false;

	/* Block until MQTT CONNACK event callback occurs */
	while (!mqtt_connected)
	{
		rc = mqtt_connect(client);
		if (rc != 0)
		{
			LOG_ERR("MQTT Connect failed [%d]", rc);
			k_msleep(MSECS_WAIT_RECONNECT);
			continue;
		}

		/* Poll MQTT socket for response */
		rc = poll_mqtt_socket(client, MSECS_NET_POLL_TIMEOUT);
		if (rc > 0)
		{
			mqtt_input(client);
		}

		if (!mqtt_connected)
		{
			mqtt_abort(client);
		}
	}
}

int app_mqtt_init(struct mqtt_client *client, char *server_addr)
{
	int rc;
	uint8_t broker_ip[NET_IPV4_ADDR_LEN];
	struct sockaddr_in *broker4;
	// struct addrinfo *result;
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM};

	/* Resolve IP address of MQTT broker */
	// rc = getaddrinfo(CONFIG_NET_SAMPLE_MQTT_BROKER_HOSTNAME,
	// 			CONFIG_NET_SAMPLE_MQTT_BROKER_PORT, &hints, &result);
	// if (rc != 0) {
	// 	LOG_ERR("Failed to resolve broker hostname [%s]", gai_strerror(rc));
	// 	return -EIO;
	// }
	// if (result == NULL) {
	// 	LOG_ERR("Broker address not found");
	// 	return -ENOENT;
	// }

	broker4 = (struct sockaddr_in *)&broker;
	inet_pton(AF_INET, server_addr, &(broker4->sin_addr.s_addr));
	// broker4->sin_addr.s_addr = addr->s_addr;
	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(SERVER_PORT);
	// freeaddrinfo(result);

	/* Log resolved IP address */
	inet_ntop(AF_INET, &broker4->sin_addr.s_addr, broker_ip, sizeof(broker_ip));
	LOG_INF("Connecting to MQTT broker @ %s", broker_ip);

	/* MQTT client configuration */
	init_mqtt_client_id();
	LOG_INF("init_mqtt_client_id");
	mqtt_client_init(client);
	LOG_INF("mqtt_client_init");

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = mqtt_event_handler;
	client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
	// client->client_id.size = strlen(MQTT_CLIENTID);
	client->client_id.size = 2; // 2 bytes is enough for the client ID since it's generated as "cuX" where X is a single digit
	client->password = NULL;
	client->user_name = NULL;

	LOG_INF("MQTT client initialized with client ID: %x", MQTT_CLIENTID);
	LOG_INF("MQTT client will topic: %s", will_param.topic.topic.utf8);
	LOG_INF("MQTT client will message: %s", will_param.message.utf8);

	/* Configure Last Will */
	client->will_topic = (struct mqtt_topic *)&will_param.topic;
	client->will_message = (struct mqtt_utf8 *)&will_param.message;
	// client->will_qos = will_param.topic.qos;
	client->will_retain = will_param.retain;

	client->keepalive = APP_MQTT_KEEPALIVE;

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

	return rc;
}
