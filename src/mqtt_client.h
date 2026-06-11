/*
 * Copyright (c) 2024 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __MQTT_CLIENT_H__
#define __MQTT_CLIENT_H__

/** MQTT connection timeouts */
#define MSECS_NET_POLL_TIMEOUT 5000
#define MSECS_WAIT_RECONNECT 1000

/** MQTT connection status flag */
extern bool mqtt_connected;

/**
 *  @brief Initialise the MQTT client & broker configuration
 */
int app_mqtt_init(struct mqtt_client *client, char *addr);

/**
 *  @brief Blocking function that establishes connectivity to the MQTT broker
 */
void app_mqtt_connect(struct mqtt_client *client);

/**
 *  @brief  Subscribes to user-defined MQTT topics and continuously
 *          processes incoming data while the MQTT connection is active
 */
void app_mqtt_run(struct mqtt_client *client);

/**
 *  @brief   Subscribe to user-defined MQTT topics
 */
int app_mqtt_subscribe(struct mqtt_client *client);

/**
 *  @brief  Publish MQTT payload
 */
int app_mqtt_publish(struct mqtt_client *client);

/**
 * @brief  Publish MQTT payload with status topic
 */
int app_mqtt_publish_status(struct mqtt_client *client);

/**
 * @brief  Publish MQTT payload with mode topic
 */
int app_mqtt_publish_mode(struct mqtt_client *client);

/**
 * @brief  Publish MQTT payload with targets topic
 */
int app_mqtt_publish_targets(struct mqtt_client *client);

/**
 * @brief  Publish MQTT payload with device descriptor topic
 */
int app_mqtt_publish_descriptor(struct mqtt_client *client);

/**
 * @brief  Publish MQTT payload with parameters topic
 */
int app_mqtt_publish_parameters(struct mqtt_client *client);

/**
 * @brief  Process incoming MQTT data and handle keepalive
 */
int app_mqtt_process(struct mqtt_client *client);

#endif /* __MQTT_CLIENT_H__ */
