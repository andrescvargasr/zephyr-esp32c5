/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define NO_SERVER_ADDR "0.0.0.0"
#define DEFAULT_SERVER_ADDR CONFIG_NET_SAMPLE_MQTT_BROKER_HOSTNAME

#ifdef CONFIG_NET_CONFIG_SETTINGS
#ifdef CONFIG_NET_IPV6
#define ZEPHYR_ADDR CONFIG_NET_CONFIG_MY_IPV6_ADDR
#define SERVER_ADDR CONFIG_NET_CONFIG_PEER_IPV6_ADDR
#else
#define ZEPHYR_ADDR CONFIG_NET_CONFIG_MY_IPV4_ADDR
#define SERVER_ADDR CONFIG_NET_CONFIG_PEER_IPV4_ADDR
#endif
#else
#ifdef CONFIG_NET_IPV6
#define ZEPHYR_ADDR "2001:db8::1"
#define SERVER_ADDR "2001:db8::2"
#else
#define ZEPHYR_ADDR "192.168.1.101"
#define SERVER_ADDR "0.0.0.0"
// #define SERVER_ADDR		"192.168.1.10"
#endif
#endif

#if defined(CONFIG_SOCKS)
#define SOCKS5_PROXY_ADDR SERVER_ADDR
#define SOCKS5_PROXY_PORT 1080
#endif

#ifdef CONFIG_MQTT_LIB_TLS
#ifdef CONFIG_MQTT_LIB_WEBSOCKET
#define SERVER_PORT 9001
#else
#define SERVER_PORT 8883
#endif /* CONFIG_MQTT_LIB_WEBSOCKET */
#else
#ifdef CONFIG_MQTT_LIB_WEBSOCKET
#define SERVER_PORT 9001
#else
#define SERVER_PORT 1883
#endif /* CONFIG_MQTT_LIB_WEBSOCKET */
#endif

#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS 500

#define APP_CONNECT_TRIES 10

#define APP_MQTT_BUFFER_SIZE 128

#define APP_MQTT_KEEPALIVE 5

// MQTT definitions
#define PRODUCT_ID "SA"     // User defined
#define SERIAL_NUMBER "pu3" // It will be extracted from EFUSE_BLK3 (USER_DATA)

#define MQTT_COMPANY "technaid_sl"
#define MQTT_PRODUCT "techstim"
#define MQTT_PUBLISH_DEVICE_DESCRIPTOR MQTT_COMPANY "/" MQTT_PRODUCT "/" SERIAL_NUMBER "/device_descriptor"
#define MQTT_PUBLISH_PARAMETERS MQTT_COMPANY "/" MQTT_PRODUCT "/" SERIAL_NUMBER "/parameters"
#define MQTT_PUBLISH_INFO MQTT_COMPANY "/" MQTT_PRODUCT "/" SERIAL_NUMBER "/info"
#define MQTT_SUBSCRIBE_IDS MQTT_COMPANY "/" MQTT_PRODUCT "/" SERIAL_NUMBER "/ids"

// #define TARGETS_ARRAY "[" SERIAL_NUMBER "," SERIAL_NUMBER "]"
#define TARGETS_ARRAY "[" SERIAL_NUMBER "," SERIAL_NUMBER "]"

/* descriptor en un único literal de bytes (Little‑Endian) */
#define DEVICE_DESCRIPTOR_BYTES                               \
    "\x26"                     /* Length = 38 */              \
    "\x00\x04"                 /* DescriptorVersion = 1024 */ \
    "\x00"                     /* DeviceClass */              \
    "\x01"                     /* DeviceSubClass */           \
    "\x01"                     /* DeviceID */                 \
    "\x02"                     /* DeviceProtocol */           \
    "\x0D"                     /* MaxPacketSize */            \
    "\x39\x30"                 /* Vendor = 12345 */           \
    "\x0D"                     /* Product */                  \
    "\x00\x04"                 /* BCDDevice = 1024 */         \
    "\x7B\x00\x00\x00"         /* SerialNumber = 123 */       \
    "\x10"                     /* IDs */                      \
    "\x00\x10"                 /* HardwareVersion = 4096 */   \
    "\x00\x04"                 /* FirmwareVersion = 1024 */   \
    "\x0D"                     /* ProtocolsSupported */       \
    "\x68\x01\xA8\xC0"         /* WIP = 192.168.1.104 */      \
    "\x00\x00\x00\x00"         /* CANID */                    \
    "\xB7\x3A\x11\x44\x1B\x00" /* MAC = 00:1B:44:11:3A:B7 */  \
    "\xB8\x0B"                 /* Port = 3000 */              \
    "\x00\xC2\x01\x00"         /* ByteSpeed = 115200 */

/* longitud en bytes */
#define DEVICE_DESCRIPTOR_LEN sizeof(DEVICE_DESCRIPTOR_BYTES) - 1

#define DEVICE_DESCRIPTOR_MACRO "{\n \
\"Length\": 38,\n \
\"DescriptorVersion\": 1024,\n \
\"DeviceClass\": 0,\n \
\"DeviceSubClass\": 1,\n \
\"DeviceID\": 1,\n \
\"DeviceProtocol\": 2,\n \
\"MaxPacketSize\": 13,\n \
\"Vendor\": 12345,\n \
\"Product\": 13,\n \
\"BCDDevice\": 1024,\n \
\"SerialNumber\": 123,\n \
\"IDs\": 16,\n \
\"HardwareVersion\": 4096,\n \
\"FirmwareVersion\": 1024,\n \
\"ProtocolsSupported\": 13,\n \
\"WIP\": 3232235880,\n \
\"CANID\": 0,\n \
\"MAC\": 117106096823,\n \
\"Port\": 3000,\n \
\"ByteSpeed\": 115200,\n \
}"

// Info status values
#define INFO_STATUS_CONNECTED "connected"
#define INFO_STATUS_DISCONNECTED "disconnected"

// Info mode values
#define INFO_MODE_CONTROLLER "controller"
#define INFO_MODE_PERIPHERAL "peripheral"

#define MQTT_CLIENTID PRODUCT_ID

#endif
